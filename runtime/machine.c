/* machine.c - máquina CPS1 sobre el código recompilado (ver machine.h). */
#include "machine.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_funcs.h"
#include "sound.h"
#include "state.h"

/* códigos con los que se vuelve a la entrada de machine_run / machine_step */
enum { RESUME_START = 0, RESUME_TRAMPOLINE = 1, RESUME_STOP = 2 };

static inline Machine *mach(M68K *c) { return (Machine *)c; }

static SF2_NORETURN void stop_machine(Machine *mc, int status) {
    mc->status = status;
    longjmp(mc->resume, RESUME_STOP);
}

void rt_fatal(M68K *c, const char *fmt, ...) {
    Machine *mc = mach(c);
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(mc->error, sizeof mc->error, "ERROR FATAL en pc=%06x (cuadro %llu, instr %llu): %s", c->pc,
             (unsigned long long)mc->cps.frame, (unsigned long long)c->icount, msg);
    stop_machine(mc, MACHINE_ERROR);
}

/* ---- planificador de la IRQ2 ---------------------------------------------------------- */
static void poll(Machine *mc) {
    M68K *c = &mc->cpu;
    if (c->cycles >= mc->next_irq) {
        mc->irq_time = mc->next_irq;    /* instante en que se afirma la IRQ2 (el VBlank) */
        mc->next_irq += mc->cps.frame_cycles;
        mc->vblanks++;
        if (mc->sound)
            sound_run_until(mc->sound, c->cycles);
        if (mc->on_vblank)
            mc->on_vblank(mc);          /* vídeo: cuadro y copia de la tabla de objetos, antes de la IRQ */
        c->irq_pending = 2;
        if (mc->yield_enabled)
            mc->yield_pending = 1;
    }
    /* Reloj de MAME (D47): la IRQ se atiende tras la instrucción anterior si se afirmó a más tardar al terminar el último
     * acceso al bus previo a su "latch" de interrupción (mc->ack, calculado en rt_hook); si no, tras esta. */
    if (c->irq_pending && c->irq_pending > c->im && (!recomp_clock_mame || mc->irq_time <= mc->ack)) {
        uint32_t ret = c->pc;
        cps1_on_irq2(&mc->cps, c);      /* cuenta el cuadro y aplica sus entradas */
        exception_interrupt(c, c->irq_pending);
        c->irq_pending = 0;
        mc->irqs_taken++;
        if (mc->max_frames && mc->cps.frame > mc->max_frames)
            stop_machine(mc, MACHINE_DONE);
        mc->in_irq++;
        c->ke = 0;                      /* la secuencia de excepción acaba con accesos al bus (E = final) */
        dispatch_call(c);               /* manejador recompilado; su rte vuelve aquí */
        mc->in_irq--;
        mc->ins_start = mc->acc_end = c->cycles;    /* la instrucción interrumpida empieza ahora, tras el rte */
        if (c->pc != ret) {
            if (mc->in_fallback)
                rt_fatal(c, "el manejador de IRQ no volvio al codigo interpretado (%06x)", ret);
            longjmp(mc->resume, RESUME_TRAMPOLINE);     /* el manejador no volvió al punto interrumpido */
        }
    }
}

void machine_poll_irq(Machine *mc) {
    poll(mc);
}

/* punto de muestra de cps1.c (fin de la lectura de entradas del manejador de IRQ2): fuera del camino rápido del gancho */
static SF2_NOINLINE void sample_point(Machine *mc, M68K *c) {
    mc->sample_frame = mc->cps.frame;
    if (mc->sound && mc->sound->f_events)
        fprintf(mc->sound->f_events, "F %llu %llu\n", (unsigned long long)mc->cps.frame, (unsigned long long)c->cycles);
    cps1_sample(&mc->cps, c);
    if (mc->on_sample)
        mc->on_sample(mc, mc->cps.frame);
}

static SF2_NOINLINE void slow_poll(Machine *mc) {
    poll(mc);
}

/* Llamado antes de cada instrucción (INSN del código generado): es lo más caliente del núcleo, así que el caso común
 * (sin VBlank, sin IRQ pendiente, sin ganchos de depuración) no llama a nada y no tiene saltos que dependan de los datos. */
void rt_hook(M68K *c) {
    Machine *mc = mach(c);
    if (SF2_UNLIKELY(mc->skip_hook)) {
        mc->skip_hook = 0;              /* reanudación tras ceder: el gancho de esta instrucción ya se ejecutó */
        return;
    }
    c->icount++;
    if (recomp_clock_mame) {
        /* la instrucción anterior acaba de terminar (c->cycles): hasta qué ciclo tenía que estar afirmada una IRQ para
         * atenderse tras ella (K) y cuándo terminó su último acceso al bus (E), según el código que dejó en c->ke
         * (bits 14-15: K desde el final, desde el comienzo o el E anterior; bits 6-7: E desde el comienzo, al final o
         * ninguno).  Selecciones en vez de switch: el tipo cambia de una instrucción a otra y un salto fallaría mucho. */
        const uint64_t s = c->cycles, sp = mc->ins_start, prev_end = mc->acc_end;
        const uint32_t ke = c->ke, k = (ke >> 8) & 63, e = ke & 63, kk = ke >> 14, ek = (ke >> 6) & 3;
        const uint64_t k_start = sp + k, k_end = s - k;
        mc->ack = kk == 0 ? prev_end : kk == 1 ? k_start : k_end;
        const uint64_t e_start = sp + e;
        mc->acc_end = ek == 0 ? s : ek == 1 ? e_start : prev_end;
        mc->ins_start = s;
    }
    if (SF2_UNLIKELY(mc->max_insns != 0) && c->icount > mc->max_insns)
        stop_machine(mc, MACHINE_DONE);
    if (SF2_UNLIKELY(c->pc == CPS1_SAMPLE_PC))
        sample_point(mc, c);
    /* poll() solo hace algo si llegó el VBlank o hay una IRQ pendiente */
    if (SF2_UNLIKELY(c->cycles >= mc->next_irq || c->irq_pending))
        slow_poll(mc);
    /* la instrucción se registra cuando de verdad va a ejecutarse: si la IRQ la interrumpió, después del manejador (como
     * el depurador de MAME), y si el manejador cambió de tarea, nunca (no se ejecuta) */
    mc->last_pc = c->pc;
    if (SF2_UNLIKELY(mc->on_insn != NULL))
        mc->on_insn(mc, c->pc);
    if (SF2_UNLIKELY(mc->yield_pending) && !mc->in_irq && rt_lookup(c->pc)) {
        mc->yield_pending = 0;
        mc->skip_hook = 1;
        stop_machine(mc, MACHINE_YIELD);
    }
}

void rt_stopped(M68K *c) {
    Machine *mc = mach(c);
    while (c->stopped) {
        c->cycles = mc->next_irq;        /* avanzar el reloj hasta la siguiente IRQ */
        poll(mc);
        if (c->stopped && c->irq_pending && c->irq_pending > c->im) {
            c->cycles = mc->irq_time + 4;   /* parado no hay instrucción en curso: se atiende en cuanto se ve */
            poll(mc);
        }
        if (c->stopped && c->im >= 2)
            rt_fatal(c, "STOP con IRQ2 enmascarada: la CPU no despertara nunca");
    }
}

void rt_unknown(M68K *c) {
    Machine *mc = mach(c);
    mc->dispatch_misses++;
    mc->miss_from = mc->last_pc;
    mc->miss_to = c->pc;
    if (mc->on_unknown && mc->on_unknown(mc))
        return;
    rt_fatal(c, "el juego llego a codigo que el recompilador no vio: salto a %06x desde la instruccion %06x "
                "(anadir 'entry %06x' a tools/hints.txt y regenerar)", mc->miss_to, mc->miss_from, mc->miss_to);
}

RecompFn rt_dispatch(M68K *c) {
    Machine *mc = mach(c);
    RecompFn f = rt_lookup(c->pc);
    if (SF2_UNLIKELY(mc->on_dispatch != NULL))
        mc->on_dispatch(mc, mc->last_pc, c->pc, f != NULL);
    if (SF2_UNLIKELY(f == NULL)) {
        rt_unknown(c);                  /* la red de seguridad de los arneses (--fallback) o rt_fatal */
        f = rt_lookup(c->pc);
        if (f == NULL)
            rt_fatal(c, "destino indirecto sin resolver: %06x", c->pc);
    }
    return f;
}

/* ---- arranque ----------------------------------------------------------------------------- */
static void cpu_reset(Machine *mc) {
    M68K *c = &mc->cpu;
    const int32_t mod_wide = c->mod_wide, mod_fixcam = c->mod_fixcam;          /* ajuste del anfitrión: el reinicio no lo cambia */
    memset(c, 0, sizeof *c);
    c->mod_wide = mod_wide;
    c->mod_fixcam = mod_fixcam;
    c->mem = &mc->mem;
    c->s = 1;
    c->im = 7;
    c->z = 1;          /* como Musashi al arrancar (CCR indefinido en hardware) */
    c->a[7] = rd32(c, 0);
    c->pc = rd32(c, 4);
    c->sp_other = 0;
    /* con el reloj de MAME (D47) la secuencia de reset dura 34 ciclos: la primera instrucción empieza en el ciclo 34 del
     * contador que también marca los VBlank (múltiplos de 167.680), como totalcycles en el depurador de MAME */
    if (recomp_clock_mame)
        c->cycles = 34;
    mc->ins_start = mc->acc_end = mc->ack = c->cycles;
}

static void sound_latch_hook(void *ctx, int which, uint8_t val) {
    Machine *mc = ctx;
    if (mc->cmd_log)
        fprintf(mc->cmd_log, "%llu %d %02x\n", (unsigned long long)mc->sample_frame, which, val);
    /* el código generado suma los ciclos de la instrucción al empezarla: el ciclo es el de su final */
    if (mc->sound)
        sound_latch_write(mc->sound, which, val, mc->cpu.cycles);
}

/* el gancho de los latches solo se instala si hay quien lo use: la placa de sonido o la lista de comandos */
static void update_latch_hook(Machine *mc) {
    mc->mem.sound_latch_cb = (mc->sound || mc->cmd_log) ? sound_latch_hook : NULL;
    mc->mem.sound_ctx = mc;
}

void machine_attach_sound(Machine *mc, struct Cps1Sound *snd) {
    mc->sound = snd;
    update_latch_hook(mc);
}

void machine_log_sound_cmds(Machine *mc, FILE *f) {
    mc->cmd_log = f;
    update_latch_hook(mc);
}

void machine_init(Machine *mc, const uint8_t *rom, uint64_t frame_cycles) {
    memset(mc, 0, sizeof *mc);
    mem_init(&mc->mem, rom, "recomp");
    cps1_init(&mc->cps, &mc->mem);
    mc->cps.frame_cycles = frame_cycles;
    mc->next_irq = frame_cycles;
    update_latch_hook(mc);
    cps1_apply_inputs(&mc->cps, 0);
    cpu_reset(mc);
}

void machine_free(Machine *mc) {
    mem_free(&mc->mem);
    free(mc->cps.events);
    mc->cps.events = NULL;
}

void machine_reset(Machine *mc) {
    mem_reset(&mc->mem);
    mc->cps.frame = 0;
    mc->next_irq = mc->cps.frame_cycles;
    mc->irq_time = 0;
    mc->vblanks = mc->irqs_taken = mc->sample_frame = 0;
    mc->status = MACHINE_RUNNING;
    mc->started = mc->yield_pending = mc->skip_hook = mc->in_irq = mc->in_fallback = 0;
    mc->last_pc = 0;
    mc->dispatch_misses = 0;
    mc->miss_from = mc->miss_to = 0;
    mc->error[0] = 0;
    if (mc->sound)
        sound_reset(mc->sound);
    cps1_apply_inputs(&mc->cps, 0);
    cpu_reset(mc);
}

/* ---- ejecución ---------------------------------------------------------------------------- */
static int execute(Machine *mc) {
    M68K *c = &mc->cpu;
    if (!mc->started) {
        mc->started = 1;
        cps1_apply_inputs(&mc->cps, 0);     /* entradas del cuadro 0 (la secuencia ya está cargada) */
    }
    int code = setjmp(mc->resume);
    if (code == RESUME_STOP)
        return mc->status;
    /* RESUME_START, o RESUME_TRAMPOLINE tras un cambio de flujo no estructurado (cambio de tarea en IRQ) */
    mc->in_irq = 0;
    for (;;) {
        RecompFn f = dispatch_fn(c);
        f(c);
    }
}

int machine_run(Machine *mc) {
    if (mc->status == MACHINE_YIELD)
        mc->status = MACHINE_RUNNING;
    if (mc->status != MACHINE_RUNNING)
        return mc->status;
    mc->yield_enabled = 0;
    mc->yield_pending = 0;
    return execute(mc);
}

int machine_step(Machine *mc) {
    if (mc->status == MACHINE_YIELD)
        mc->status = MACHINE_RUNNING;
    if (mc->status != MACHINE_RUNNING)
        return mc->status;
    mc->yield_enabled = 1;
    return execute(mc);
}

/* ---- estado guardable ---------------------------------------------------------------------- */
void machine_state(Machine *mc, StateStream *s) {
    M68K *c = &mc->cpu;
    for (int i = 0; i < 8; i++) st_u32(s, &c->d[i]);
    for (int i = 0; i < 8; i++) st_u32(s, &c->a[i]);
    st_u32(s, &c->pc);
    st_u32(s, &c->sp_other);
    uint8_t *flags[] = { &c->x, &c->n, &c->z, &c->v, &c->c, &c->s, &c->t, &c->stopped };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        st_u8(s, flags[i]);
        st_check(s, *flags[i] <= 1);
    }
    st_u8(s, &c->im);
    st_check(s, c->im <= 7);
    st_u8(s, &c->irq_pending);
    st_check(s, c->irq_pending <= 7);
    st_u64(s, &c->icount);
    st_u64(s, &c->cycles);
    st_u64(s, &mc->next_irq);
    st_u64(s, &mc->irq_time);
    st_u64(s, &mc->ins_start);
    st_u64(s, &mc->acc_end);
    st_u64(s, &mc->ack);
    st_u16(s, &c->ke);
    st_u64(s, &mc->vblanks);
    st_u64(s, &mc->irqs_taken);
    st_u64(s, &mc->sample_frame);
    uint8_t skip = (uint8_t)(mc->skip_hook != 0);
    st_u8(s, &skip);
    st_check(s, skip <= 1);
    mem_state(&mc->mem, s);
    cps1_state(&mc->cps, s);
    uint8_t has_sound = (uint8_t)(mc->sound != NULL);
    st_u8(s, &has_sound);
    st_check(s, has_sound == (uint8_t)(mc->sound != NULL));      /* el estado y la máquina deben coincidir en llevar sonido */
    if (mc->sound)
        sound_state(mc->sound, s);
    if (!s->saving && !s->error) {
        /* el punto de reanudación debe ser una dirección de la tabla de despacho (se cede solo en ellas) */
        st_check(s, rt_lookup(c->pc) != NULL);
        mc->skip_hook = skip;
        mc->started = 1;
        mc->yield_pending = 0;
        mc->in_irq = mc->in_fallback = 0;
        mc->status = MACHINE_RUNNING;
        mc->last_pc = c->pc;
        mc->error[0] = 0;
    }
}
