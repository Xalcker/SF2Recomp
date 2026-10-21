#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_funcs.h"
#include "common.h"
#include "m68k.h"
#include "musashi_glue.h"
#include "sound.h"

static const char *g_cov_path, *g_io_table_path, *g_dispatch_path;

/* despachos por la tabla (hito 6): pares (instrucción de origen, destino) con su cuenta, en una tabla hash abierta */
typedef struct DispatchPair { uint32_t from, to; uint64_t count; int known; } DispatchPair;
static DispatchPair *g_disp;
static size_t g_disp_cap, g_disp_n;
static uint64_t g_disp_total, g_disp_unknown;

/* registro de episodios de Musashi */
typedef struct Fallback { uint32_t entry, exit; uint64_t insns, count; } Fallback;
static Fallback g_fallbacks[256];
static unsigned g_nfallbacks;
static uint64_t g_fallback_insns, g_fallback_episodes;

void harness_set_report_paths(const char *cov_path, const char *io_table_path) {
    g_cov_path = cov_path;
    g_io_table_path = io_table_path;
}

static size_t disp_slot(DispatchPair *t, size_t cap, uint32_t from, uint32_t to) {
    uint64_t h = ((uint64_t)from * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)to * 0xC2B2AE3D27D4EB4Full);
    size_t i = (size_t)(h ^ (h >> 29)) & (cap - 1);
    while (t[i].count && (t[i].from != from || t[i].to != to))
        i = (i + 1) & (cap - 1);
    return i;
}

static void on_dispatch(Machine *mc, uint32_t from, uint32_t to, int known) {
    (void)mc;
    g_disp_total++;
    if (!known) g_disp_unknown++;
    if (g_disp_n * 2 >= g_disp_cap) {
        size_t ncap = g_disp_cap ? g_disp_cap * 2 : 4096;
        DispatchPair *nt = calloc(ncap, sizeof *nt);
        if (!nt) return;
        for (size_t i = 0; i < g_disp_cap; i++)
            if (g_disp[i].count) nt[disp_slot(nt, ncap, g_disp[i].from, g_disp[i].to)] = g_disp[i];
        free(g_disp);
        g_disp = nt;
        g_disp_cap = ncap;
    }
    size_t i = disp_slot(g_disp, g_disp_cap, from, to);
    if (!g_disp[i].count) {
        g_disp[i].from = from;
        g_disp[i].to = to;
        g_disp[i].known = known;
        g_disp_n++;
    }
    g_disp[i].count++;
}

void harness_set_dispatch_log(Machine *mc, const char *path) {
    g_dispatch_path = path;
    mc->on_dispatch = path ? on_dispatch : NULL;
}

static int cmp_pair(const void *x, const void *y) {
    const DispatchPair *a = x, *b = y;
    if (a->to != b->to) return a->to < b->to ? -1 : 1;
    return a->from < b->from ? -1 : a->from > b->from;
}

static void dispatch_report(void) {
    if (!g_dispatch_path)
        return;
    DispatchPair *v = malloc((g_disp_n ? g_disp_n : 1) * sizeof *v);
    size_t n = 0;
    for (size_t i = 0; i < g_disp_cap; i++)
        if (g_disp[i].count) v[n++] = g_disp[i];
    qsort(v, n, sizeof *v, cmp_pair);
    FILE *f = fopen(g_dispatch_path, "w");
    if (f) {
        fprintf(f, "# despachos por la tabla: origen destino veces en_tabla (hito 6, tools/coverage.py)\n");
        for (size_t i = 0; i < n; i++)
            fprintf(f, "%06x %06x %llu %d\n", v[i].from, v[i].to, (unsigned long long)v[i].count, v[i].known);
        fclose(f);
    }
    printf("despachos por la tabla: %llu (%zu pares origen-destino distintos, %llu a direcciones fuera de la tabla) en %s\n",
           (unsigned long long)g_disp_total, n, (unsigned long long)g_disp_unknown, g_dispatch_path);
    free(v);
}

void harness_progress(Machine *mc, uint64_t frame) {
    if (!mc->quiet && frame % 600 == 0)
        fprintf(stderr, "  cuadro %llu: %llu instrucciones\n", (unsigned long long)frame, (unsigned long long)mc->cpu.icount);
}

/* --trace-cycles (hito 6): "pc ciclo cuadro" de cada instrucción de los cuadros [desde, hasta], el ciclo al empezarla */
static FILE *g_trace_cyc;
static uint64_t g_trace_from, g_trace_to;

void harness_set_cycle_trace(FILE *f, uint64_t from, uint64_t to) {
    g_trace_cyc = f;
    g_trace_from = from;
    g_trace_to = to;
}

static void on_insn(Machine *mc, uint32_t pc) {
    ring_push(pc);
    cov_mark(pc);
    if (g_trace_cyc && mc->cps.frame >= g_trace_from && mc->cps.frame <= g_trace_to)
        fprintf(g_trace_cyc, "%06x %llu %llu\n", pc, (unsigned long long)mc->cpu.cycles, (unsigned long long)mc->cps.frame);
}

/* ---- red de seguridad: Musashi sobre la misma memoria ---------------------------------------- */
static void musashi_load(M68K *c) {
    for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_D0 + i, c->d[i]);
    for (int i = 0; i < 7; i++) m68k_set_reg(M68K_REG_A0 + i, c->a[i]);
    m68k_set_reg(M68K_REG_SR, get_sr(c));          /* fija S sin tocar las pilas */
    m68k_set_reg(M68K_REG_SP, c->a[7]);
    if (c->s) m68k_set_reg(M68K_REG_USP, c->sp_other);
    else      m68k_set_reg(M68K_REG_ISP, c->sp_other);
    m68k_set_reg(M68K_REG_PC, c->pc);
}

static void musashi_store(M68K *c) {
    for (int i = 0; i < 8; i++) c->d[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
    for (int i = 0; i < 7; i++) c->a[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
    c->t = (sr >> 15) & 1;
    c->s = (sr >> 13) & 1;
    c->im = (sr >> 8) & 7;
    set_ccr(c, sr);
    c->a[7] = m68k_get_reg(NULL, M68K_REG_SP);
    c->sp_other = c->s ? m68k_get_reg(NULL, M68K_REG_USP) : m68k_get_reg(NULL, M68K_REG_ISP);
    c->pc = m68k_get_reg(NULL, M68K_REG_PC);
}

static int on_unknown(Machine *mc) {
    M68K *c = &mc->cpu;
    uint32_t entry = c->pc;
    uint64_t n = 0;
    if (mc->in_fallback)
        rt_fatal(c, "reentrada en la red de seguridad desde %06x", entry);
    mc->in_fallback = 1;
    musashi_load(c);
    for (;;) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (pc >= ROM_SIZE)
            rt_fatal(c, "Musashi fuera de la ROM en %06x (entrada %06x)", pc, entry);
        ring_push(pc);
        int used = m68k_execute(1);
        c->cycles += (uint64_t)used;
        c->icount++;
        n++;
        pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (rt_lookup(pc)) {
            musashi_store(c);
            break;
        }
        if (c->cycles >= mc->next_irq || (c->irq_pending && c->irq_pending > ((m68k_get_reg(NULL, M68K_REG_SR) >> 8) & 7))) {
            /* la IRQ la atiende siempre el manejador recompilado */
            musashi_store(c);
            machine_poll_irq(mc);
            musashi_load(c);
        }
        if (n > 50000000ull)
            rt_fatal(c, "Musashi no vuelve a codigo recompilado desde %06x (ahora en %06x)", entry, pc);
    }
    mc->in_fallback = 0;
    g_fallback_insns += n;
    g_fallback_episodes++;
    unsigned i;
    for (i = 0; i < g_nfallbacks; i++)
        if (g_fallbacks[i].entry == entry && g_fallbacks[i].exit == c->pc)
            break;
    if (i == g_nfallbacks && g_nfallbacks < 256) {
        g_fallbacks[g_nfallbacks++] = (Fallback){ entry, c->pc, 0, 0 };
    }
    if (i < g_nfallbacks) { g_fallbacks[i].insns += n; g_fallbacks[i].count++; }
    if (!mc->quiet && g_fallback_episodes <= 20)
        fprintf(stderr, "  Musashi: %06x -> %06x, %llu instrucciones (cuadro %llu)\n", entry, c->pc,
                (unsigned long long)n, (unsigned long long)mc->cps.frame);
    return 1;
}

void harness_install(Machine *mc, int fallback) {
    mc->on_insn = on_insn;
    mc->on_unknown = fallback ? on_unknown : NULL;
    g_musashi_mem_ptr = &mc->mem;
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    m68k_pulse_reset();
}

static void close_outputs(Machine *mc) {
    if (mc->cmd_log) {
        fprintf(mc->cmd_log, "# cuadros %llu\n", (unsigned long long)mc->sample_frame);
        fclose(mc->cmd_log);
        mc->cmd_log = NULL;
    }
    if (mc->cps.summary && mc->cps.summary != stdout) {
        fclose(mc->cps.summary);
        mc->cps.summary = NULL;
    }
}

static void finish(Machine *mc, const char *why, void (*on_finish)(Machine *, const char *)) {
    M68K *c = &mc->cpu;
    printf("%s: %llu cuadros, %llu instrucciones (%llu en Musashi, %llu episodios), %llu ciclos estimados, "
           "%llu IRQ atendidas, %llu lineas de resumen\n", why, (unsigned long long)mc->cps.frame,
           (unsigned long long)c->icount, (unsigned long long)g_fallback_insns, (unsigned long long)g_fallback_episodes,
           (unsigned long long)c->cycles, (unsigned long long)mc->irqs_taken, (unsigned long long)mc->cps.samples);
    printf("accesos E/S: %llu lecturas, %llu escrituras, %llu sin mapear\n", (unsigned long long)mc->mem.io_reads,
           (unsigned long long)mc->mem.io_writes, (unsigned long long)mc->mem.unmapped);
    if (g_nfallbacks) {
        printf("codigo ejecutado por Musashi (anadir 'entry' a tools/hints.txt):\n");
        for (unsigned i = 0; i < g_nfallbacks; i++)
            printf("  entrada %06x -> salida %06x: %llu instrucciones en %llu episodios\n", g_fallbacks[i].entry,
                   g_fallbacks[i].exit, (unsigned long long)g_fallbacks[i].insns, (unsigned long long)g_fallbacks[i].count);
    }
    cov_report(g_cov_path, recomp_insn_count);
    dispatch_report();
    if (g_io_table_path) {
        FILE *f = fopen(g_io_table_path, "w");
        if (f) { mem_print_io_table(&mc->mem, f); fclose(f); printf("tabla de E/S escrita en %s\n", g_io_table_path); }
    }
    if (mc->sound) {
        sound_run_until(mc->sound, mc->cpu.cycles);
        sound_report(mc->sound, stdout);
    }
    if (on_finish)
        on_finish(mc, why);
    close_outputs(mc);
}

void harness_run(Machine *mc, void (*on_finish)(Machine *mc, const char *why)) {
    int st = machine_run(mc);
    if (st == MACHINE_ERROR) {
        M68K *c = &mc->cpu;
        fprintf(stderr, "\n%s\n", mc->error);
        dispatch_report();
        cov_report(g_cov_path, recomp_insn_count);
        dump_regs(stderr, "recomp", c->d, c->a, c->pc, get_sr(c));
        ring_dump(stderr, musashi_disasm);
        close_outputs(mc);
        exit(2);
    }
    const char *why = (mc->max_insns && mc->cpu.icount > mc->max_insns) ? "limite de instrucciones" : "OK";
    finish(mc, why, on_finish);
    exit(0);
}
