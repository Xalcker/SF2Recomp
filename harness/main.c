/*
 * main.c - arnés de validación en paso cerrado (recompilado frente a Musashi).
 *
 *   sf2_lockstep rom.bin [--frames N] [--irq-frames N] [--inputs archivo] [--summary archivo] [--detail]
 *                [--frame-cycles N] [--io-table archivo] [--trace] [--log-io] [--quiet] [--cov archivo]
 *
 *   Ejecuta el código recompilado y Musashi instrucción a instrucción con memorias separadas, marca la
 *   IRQ2 pendiente cada `frame-cycles` ciclos de Musashi (167.680 por defecto, el cuadro de MAME) y la
 *   entrega en ambos lados en el mismo punto, y se detiene en la primera divergencia de registros o de
 *   escrituras.  Con --inputs y --summary reproduce la misma secuencia de entradas y escribe el mismo
 *   resumen por cuadro que build/sf2_blind, con el reloj exacto de Musashi en vez del estimado.
 *
 *   --frames cuenta periodos de VBlank (criterio del hito 1); --irq-frames cuenta entregas de IRQ2 (los
 *   cuadros del resumen) y termina al llegar a N.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "cpu.h"
#include "cps1.h"
#include "recomp_funcs.h"
#include "common.h"
#include "hostio.h"
#include "m68k.h"
#include "musashi_glue.h"

static M68K g_cpu;
static Mem g_mem;
static Cps1 g_machine;
static jmp_buf g_resume;
static uint64_t g_cycles;          /* ciclos acumulados de Musashi */
static uint64_t g_next_irq;
static uint64_t g_frames, g_max_frames = 600, g_max_irq_frames;
static uint64_t g_max_insns;
static int g_trace, g_quiet;
static uint64_t g_irqs_taken;
static int g_check_mem, g_finish;
static const char *g_cov_path, *g_io_table_path;
static int g_cycle_check;              /* --cycle-check: comparar el coste estimado de cada instrucción con Musashi */
static uint64_t g_prev_cycles;
static int g_have_prev, g_mus_prev_used;
static uint32_t g_mus_prev_pc;

static void cycle_report(void);

static void close_outputs(void) {
    if (g_machine.summary && g_machine.summary != stdout)
        fclose(g_machine.summary);
    if (g_io_table_path) {
        FILE *f = fopen(g_io_table_path, "w");
        if (f) { mem_print_io_table(&g_mem, f); fclose(f); printf("tabla de E/S escrita en %s\n", g_io_table_path); }
    }
}

void rt_fatal(M68K *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\nERROR FATAL en pc=%06x (cuadro %llu, instr %llu): ", c->pc,
            (unsigned long long)g_frames, (unsigned long long)c->icount);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    dump_regs(stderr, "recomp", c->d, c->a, c->pc, get_sr(c));
    ring_dump(stderr, musashi_disasm);
    close_outputs();
    exit(2);
}

/* en paso cerrado no hay red de seguridad: una dirección desconocida es un fallo del análisis */
void rt_unknown(M68K *c) {
    rt_fatal(c, "retorno/salto a una direccion sin etiqueta: %06x (anadir 'entry %06x' o 'label %06x' a tools/hints.txt)",
             c->pc, c->pc, c->pc);
}

RecompFn rt_dispatch(M68K *c) {
    RecompFn f = rt_lookup(c->pc);
    if (!f)
        rt_unknown(c);
    return f;
}

static int cmp_wlog(const void *x, const void *y) {
    const WriteLog *a = x, *b = y;
    if (a->addr != b->addr) return a->addr < b->addr ? -1 : 1;
    return (int)a->val - (int)b->val;
}

static void report_divergence(M68K *c, const char *what) {
    uint32_t md[8], ma[8];
    for (int i = 0; i < 8; i++) {
        md[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
        ma[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    }
    uint32_t mpc = m68k_get_reg(NULL, M68K_REG_PC), msr = m68k_get_reg(NULL, M68K_REG_SR);
    char buf[128];
    fprintf(stderr, "\nDIVERGENCIA (%s) en el cuadro %llu, instruccion %llu, %llu ciclos\n", what,
            (unsigned long long)g_frames, (unsigned long long)c->icount, (unsigned long long)g_cycles);
    musashi_disasm(buf, sizeof buf, c->pc);
    fprintf(stderr, "siguiente instruccion (recomp)  %06x: %s\n", c->pc, buf);
    if (mpc != c->pc) {
        musashi_disasm(buf, sizeof buf, mpc);
        fprintf(stderr, "siguiente instruccion (musashi) %06x: %s\n", mpc, buf);
    }
    uint32_t prev = g_ring[(g_ring_pos + TRACE_RING - 1) % TRACE_RING];
    musashi_disasm(buf, sizeof buf, prev);
    fprintf(stderr, "ultima instruccion ejecutada    %06x: %s\n", prev, buf);
    dump_regs(stderr, "recomp", c->d, c->a, c->pc, get_sr(c));
    dump_regs(stderr, "musashi", md, ma, mpc, msr);
    fprintf(stderr, "          usp/ssp recomp: %08x   musashi usp=%08x isp=%08x\n", c->sp_other,
            m68k_get_reg(NULL, M68K_REG_USP), m68k_get_reg(NULL, M68K_REG_ISP));
    for (int i = 0; i < 8; i++) {
        if (md[i] != c->d[i]) fprintf(stderr, "  d%d difiere: recomp %08x musashi %08x\n", i, c->d[i], md[i]);
        if (ma[i] != c->a[i]) fprintf(stderr, "  a%d difiere: recomp %08x musashi %08x\n", i, c->a[i], ma[i]);
    }
    if (msr != get_sr(c)) fprintf(stderr, "  sr difiere: recomp %04x musashi %04x\n", get_sr(c), msr);
    if (g_mem.nlog || g_musashi_mem.nlog) {
        fprintf(stderr, "  escrituras recomp:");
        for (int i = 0; i < g_mem.nlog; i++) fprintf(stderr, " [%06x]=%04x", g_mem.wlog[i].addr, g_mem.wlog[i].val);
        fprintf(stderr, "\n  escrituras musashi:");
        for (int i = 0; i < g_musashi_mem.nlog; i++) fprintf(stderr, " [%06x]=%04x", g_musashi_mem.wlog[i].addr, g_musashi_mem.wlog[i].val);
        fprintf(stderr, "\n");
    }
    ring_dump(stderr, musashi_disasm);
    close_outputs();
    exit(1);
}

static void compare_state(M68K *c) {
    for (int i = 0; i < 8; i++) {
        if (m68k_get_reg(NULL, M68K_REG_D0 + i) != c->d[i] || m68k_get_reg(NULL, M68K_REG_A0 + i) != c->a[i])
            report_divergence(c, "registros");
    }
    if (m68k_get_reg(NULL, M68K_REG_PC) != c->pc || m68k_get_reg(NULL, M68K_REG_SR) != get_sr(c))
        report_divergence(c, "pc/sr");
    if (g_mem.nlog != g_musashi_mem.nlog)
        report_divergence(c, "numero de escrituras");
    if (g_mem.nlog) {
        qsort(g_mem.wlog, g_mem.nlog, sizeof(WriteLog), cmp_wlog);
        qsort(g_musashi_mem.wlog, g_musashi_mem.nlog, sizeof(WriteLog), cmp_wlog);
        if (memcmp(g_mem.wlog, g_musashi_mem.wlog, g_mem.nlog * sizeof(WriteLog)))
            report_divergence(c, "escrituras en memoria");
    }
    mem_reset_log(&g_mem);
    mem_reset_log(&g_musashi_mem);
}

static void finish(M68K *c) {
    int64_t diff = mem_compare(&g_mem, &g_musashi_mem);
    printf("OK: %llu cuadros sin divergencia (%llu entregas de IRQ2), %llu instrucciones, %llu ciclos, "
           "memoria %s\n", (unsigned long long)g_frames, (unsigned long long)g_irqs_taken,
           (unsigned long long)c->icount, (unsigned long long)g_cycles, diff < 0 ? "identica" : "DIFERENTE");
    if (diff >= 0)
        printf("primera diferencia de memoria en %06llx\n", (unsigned long long)diff);
    printf("accesos E/S: %llu lecturas, %llu escrituras, %llu sin mapear\n",
           (unsigned long long)g_mem.io_reads, (unsigned long long)g_mem.io_writes, (unsigned long long)g_mem.unmapped);
    printf("reloj estimado por el codigo generado: %llu ciclos (Musashi %llu, diferencia %lld)\n",
           (unsigned long long)c->cycles, (unsigned long long)g_cycles, (long long)(c->cycles - g_cycles));
    if (g_machine.summary)
        printf("resumen: %llu lineas\n", (unsigned long long)g_machine.samples);
    cov_report(g_cov_path, recomp_insn_count);
    cycle_report();
    close_outputs();
    exit(diff < 0 ? 0 : 3);
}

static void vblank(M68K *c) {
    g_next_irq += g_machine.frame_cycles;
    g_frames++;
    c->irq_pending = 2;
    m68k_set_irq(2);
    if (!g_quiet && g_frames % 600 == 0)
        fprintf(stderr, "  cuadro %llu: %llu instrucciones\n", (unsigned long long)g_frames,
                (unsigned long long)c->icount);
    if (g_frames % 60 == 0)
        g_check_mem = 1;       /* comparar RAM completa en el siguiente limite de instruccion */
    if (!g_max_irq_frames && g_frames >= g_max_frames)
        g_finish = 1;
}

static void step_musashi(M68K *c) {
    g_mus_prev_pc = c->pc;
    int used = m68k_execute(1);
    g_mus_prev_used = used;
    g_cycles += (uint64_t)used;
    if (g_cycles >= g_next_irq)
        vblank(c);
}

/* Entrega la IRQ pendiente en ambos lados (si la máscara lo permite) y ejecuta el manejador
 * recompilado anidado hasta su rte. */
static void deliver_irq(M68K *c) {
    if (!c->irq_pending)
        return;
    if (c->irq_pending <= c->im)
        return;
    uint32_t ret = c->pc;
    cps1_on_irq2(&g_machine, c);          /* cuenta el cuadro y aplica sus entradas en g_mem... */
    mem_copy_inputs(&g_musashi_mem, &g_mem);   /* ...y en la memoria de Musashi */
    exception_interrupt(c, c->irq_pending);
    c->irq_pending = 0;
    g_irqs_taken++;
    uint32_t mpc = m68k_get_reg(NULL, M68K_REG_PC);
    musashi_check_irq();              /* Musashi toma la interrupción sin ejecutar nada... */
    if (m68k_get_reg(NULL, M68K_REG_PC) != mpc)
        g_cycles += 44;               /* ...y m68k_execute no contabiliza su coste; si ya la tomó dentro de la
                                         instrucción anterior (move a SR, rte que desenmascaran), ya está contado */
    g_prev_cycles = c->cycles;        /* la comprobación de ciclos no debe mezclar la IRQ con la instrucción anterior */
    g_have_prev = 0;
    if (g_max_irq_frames && g_machine.frame > g_max_irq_frames)
        finish(c);
    dispatch_call(c);                 /* manejador recompilado; su rte devuelve el control */
    if (c->pc != ret)
        longjmp(g_resume, 1);
}

/* ---- comprobación del reloj estimado frente al de Musashi, instrucción a instrucción ---------- */
typedef struct CycMismatch { char mnem[12]; uint64_t count; int64_t sum; } CycMismatch;
static CycMismatch g_cyc_hist[64];
static unsigned g_cyc_nhist;
static uint64_t g_cyc_mismatches, g_cyc_checked;

static void cycle_check(M68K *c) {
    uint64_t delta = c->cycles - g_prev_cycles;
    g_cyc_checked++;
    if (delta == (uint64_t)g_mus_prev_used)
        return;
    /* Musashi toma la IRQ dentro de la instrucción que la desenmascara (+44); el recompilado la entrega
     * en este límite y suma los 44 en exception_interrupt */
    if (c->irq_pending && c->irq_pending > c->im && delta + 44 == (uint64_t)g_mus_prev_used)
        return;
    g_cyc_mismatches++;
    char buf[128];
    musashi_disasm(buf, sizeof buf, g_mus_prev_pc);
    if (g_cyc_mismatches <= 12)
        fprintf(stderr, "  ciclos distintos en %06x: %-32s recomp %llu musashi %d\n", g_mus_prev_pc, buf,
                (unsigned long long)delta, g_mus_prev_used);
    char mnem[12] = {0};
    sscanf(buf, "%11s", mnem);
    unsigned i;
    for (i = 0; i < g_cyc_nhist; i++)
        if (!strcmp(g_cyc_hist[i].mnem, mnem)) break;
    if (i == g_cyc_nhist && g_cyc_nhist < 64) { strcpy(g_cyc_hist[g_cyc_nhist].mnem, mnem); g_cyc_nhist++; }
    if (i < g_cyc_nhist) { g_cyc_hist[i].count++; g_cyc_hist[i].sum += (int64_t)delta - g_mus_prev_used; }
}

static void cycle_report(void) {
    if (!g_cycle_check) return;
    printf("comprobacion de ciclos: %llu instrucciones, %llu con coste distinto\n",
           (unsigned long long)g_cyc_checked, (unsigned long long)g_cyc_mismatches);
    for (unsigned i = 0; i < g_cyc_nhist; i++)
        printf("  %-10s %8llu veces, diferencia acumulada %lld\n", g_cyc_hist[i].mnem,
               (unsigned long long)g_cyc_hist[i].count, (long long)g_cyc_hist[i].sum);
}

void rt_hook(M68K *c) {
    c->icount++;
    ring_push(c->pc);
    cov_mark(c->pc);
    if (g_max_insns && c->icount > g_max_insns) {
        printf("limite de instrucciones alcanzado sin divergencia (%llu cuadros)\n", (unsigned long long)g_frames);
        cov_report(g_cov_path, recomp_insn_count);
        cycle_report();
        close_outputs();
        exit(0);
    }
    if (g_cycle_check && g_have_prev)
        cycle_check(c);
    deliver_irq(c);
    compare_state(c);
    if (g_check_mem) {
        g_check_mem = 0;
        int64_t diff = mem_compare(&g_mem, &g_musashi_mem);
        if (diff >= 0) {
            fprintf(stderr, "memoria distinta en %06llx\n", (unsigned long long)diff);
            report_divergence(c, "contenido de memoria");
        }
    }
    if (g_finish)
        finish(c);
    if (c->pc == CPS1_SAMPLE_PC)
        cps1_sample(&g_machine, c);
    if (g_trace) {
        char buf[128];
        musashi_disasm(buf, sizeof buf, c->pc);
        fprintf(stderr, "%06x: %-40s d0=%08x d1=%08x a0=%08x a7=%08x sr=%04x\n", c->pc, buf, c->d[0], c->d[1],
                c->a[0], c->a[7], get_sr(c));
    }
    step_musashi(c);
    g_prev_cycles = c->cycles;
    g_have_prev = 1;
}

void rt_stopped(M68K *c) {
    /* Musashi también está parado: consumir ciclos hasta la siguiente IRQ y entregarla */
    while (c->stopped) {
        if (c->im >= 2)
            rt_fatal(c, "STOP con IRQ2 enmascarada: la CPU no despertara nunca");
        int used = m68k_execute((int)(g_next_irq - g_cycles));
        g_cycles += (uint64_t)used;
        if (g_cycles >= g_next_irq)
            vblank(c);
        deliver_irq(c);   /* la IRQ apila c->pc, que ya es la instrucción siguiente al STOP */
    }
}

/* ---- arranque ----------------------------------------------------------------------------- */
static void cpu_reset(M68K *c) {
    memset(c, 0, sizeof *c);
    c->mem = &g_mem;
    c->s = 1;
    c->im = 7;
    c->z = 1;          /* Musashi arranca con not_z_flag = 0, es decir Z=1 (CCR indefinido en hardware) */
    c->a[7] = rd32(c, 0);
    c->pc = rd32(c, 4);
    c->sp_other = 0;
}

static void run(M68K *c) {
    if (setjmp(g_resume) != 0) {
        /* reanudación tras un cambio de flujo no estructurado (cambio de tarea en IRQ) */
    }
    for (;;) {
        RecompFn f = dispatch_fn(c);
        f(c);
    }
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *inputs = NULL, *summary = NULL;
    uint64_t frame_cycles = CPS1_FRAME_CYCLES;
    int log_io = 0, detail = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_max_frames = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--irq-frames") && i + 1 < argc) g_max_irq_frames = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--insns") && i + 1 < argc) g_max_insns = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--inputs") && i + 1 < argc) inputs = argv[++i];
        else if (!strcmp(argv[i], "--summary") && i + 1 < argc) summary = argv[++i];
        else if (!strcmp(argv[i], "--detail")) detail = 1;
        else if (!strcmp(argv[i], "--frame-cycles") && i + 1 < argc) frame_cycles = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--io-table") && i + 1 < argc) g_io_table_path = argv[++i];
        else if (!strcmp(argv[i], "--trace")) g_trace = 1;
        else if (!strcmp(argv[i], "--cycle-check")) g_cycle_check = 1;
        else if (!strcmp(argv[i], "--quiet")) g_quiet = 1;
        else if (!strcmp(argv[i], "--log-io")) log_io = 1;
        else if (!strcmp(argv[i], "--cov") && i + 1 < argc) g_cov_path = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "opcion desconocida: %s\n", argv[i]); return 1; }
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "uso: %s rom.bin [--frames N] [--irq-frames N] [--inputs archivo] [--summary archivo] [--detail]\n"
                        "        [--frame-cycles N] [--io-table archivo] [--insns N] [--trace] [--cycle-check] [--log-io] [--quiet] [--cov archivo]\n", argv[0]);
        return 1;
    }
    uint8_t *rom = load_rom(rom_path);
    if (!rom) return 1;

    mem_init(&g_mem, rom, "recomp");
    g_mem.log_io = log_io;
    g_mem.log_file = stderr;
    cps1_init(&g_machine, &g_mem);
    g_machine.frame_cycles = frame_cycles;
    g_machine.detail = detail;
    g_next_irq = frame_cycles;
    if (inputs) {
        int n = cps1_load_inputs(&g_machine, inputs);
        if (n < 0) { fprintf(stderr, "no se pudo leer la secuencia de entradas %s\n", inputs); return 1; }
        if (!g_quiet) fprintf(stderr, "entradas: %d eventos de %s\n", n, inputs);
    }
    if (summary) {
        g_machine.summary = !strcmp(summary, "-") ? stdout : fopen(summary, "w");
        if (!g_machine.summary) { perror(summary); return 1; }
    }
    cps1_apply_inputs(&g_machine, 0);
    cpu_reset(&g_cpu);
    if (g_cpu.pc != recomp_reset_pc)
        fprintf(stderr, "aviso: vector de reset %06x distinto del usado al generar (%06x)\n", g_cpu.pc, recomp_reset_pc);

    mem_init(&g_musashi_mem, rom, "musashi");
    g_musashi_mem.log_io = 0;
    mem_copy_inputs(&g_musashi_mem, &g_mem);
    g_musashi_mem_ptr = &g_musashi_mem;
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    m68k_pulse_reset();
    m68k_execute(0);               /* consume los ciclos de la secuencia de reset sin ejecutar nada */
    mem_reset_log(&g_mem);
    mem_reset_log(&g_musashi_mem);
    if (!g_quiet)
        fprintf(stderr, "lockstep: recompilado vs Musashi, %llu cuadros%s, IRQ2 cada %llu ciclos\n",
                (unsigned long long)(g_max_irq_frames ? g_max_irq_frames : g_max_frames),
                g_max_irq_frames ? " (entregas de IRQ2)" : "", (unsigned long long)frame_cycles);
    run(&g_cpu);
    return 0;
}
