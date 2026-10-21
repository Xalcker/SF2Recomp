/* sound.c - placa de sonido de la CPS1 (ver sound.h). */
#include "sound.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

/* ---- remuestreador "lofi" de MAME (resampler.cpp) ------------------------------------------
 * Interpolación de 4 puntos con los polinomios f0(t) = (t - t^3)/6 y f1(t) = t + (t^2 - t^3)/2 tabulados en
 * 4096 fases, y media móvil de `divide` muestras cuando la fuente es más rápida que el destino.  Las
 * expresiones en float se escriben igual que en MAME para que el compilador produzca las mismas operaciones
 * (incluida la contracción a FMA): la mezcla tiene que coincidir bit a bit. */
static float interp[2][0x1001];
static int interp_ready;

/* MAME evalúa esta tabla al compilar (queda en __TEXT,__const del binario), sin contracción a FMA; el
 * remuestreo en sí sí se compila con FMA (fmul, fmsub, fmadd, fmsub, fmadd en arm64, igual aquí) */
static void interp_init(void) {
#pragma STDC FP_CONTRACT OFF
    for (uint32_t i = 1; i != 4096; i++) {
        float p = i / 4096.0;
        interp[0][i] = (p - p*p*p) / 6;
    }
    for (uint32_t i = 1; i != 2049; i++) {
        float p = i / 4096.0;
        interp[1][i] = p + (p*p - p*p*p) / 2;
    }
    for (uint32_t i = 2049; i != 4096; i++)
        interp[1][i] = 1.0 + interp[0][i] + interp[0][4096-i] - interp[1][4096-i];
    interp[0][     0] = 0.0;
    interp[0][0x1000] = 0.0;
    interp[1][     0] = 0.0;
    interp[1][0x1000] = 1.0;
    interp_ready = 1;
}

static void lofi_init(SoundLofi *r, uint32_t fs, uint32_t ft) {
    r->fs = fs;
    r->ft = ft;
    r->divide = fs <= ft ? 1 : 1 + fs / ft;
    r->step = (uint32_t)((uint64_t)fs * 0x1000000 / ft / r->divide);
}

typedef struct RingReader { const float *ring; int64_t pos; uint32_t count; } RingReader;

static inline float ring_at(const float *ring, int64_t i) {
    return i < 0 ? 0.0f : ring[(uint64_t)i & (SOUND_RING - 1)];
}

static inline float reader_next(RingReader *rr) {
    if (rr->count == 1)
        return ring_at(rr->ring, rr->pos++);
    float sm = 0;
    for (uint32_t i = 0; i != rr->count; i++)
        sm += ring_at(rr->ring, rr->pos++);
    return sm / rr->count;
}

/* audio_resampler_lofi::apply (búfer plano de float -> entrada del altavoz) */
static void lofi_apply(const SoundLofi *r, const float *ring, float *d, uint64_t dest_sample, float gain, uint32_t samples) {
    uint32_t seconds = (uint32_t)(dest_sample / r->ft);
    uint32_t dsamp = (uint32_t)(dest_sample % r->ft);
    uint64_t ssamp = ((uint64_t)dsamp * r->fs * 0x1000) / r->ft;
    uint64_t ssample = (ssamp >> 12) + (uint64_t)r->fs * seconds;
    uint32_t phase = ssamp & 0xfff;
    if (r->divide > 1) {
        uint32_t delta = (uint32_t)(ssample % r->divide);
        phase = (phase | (delta << 12)) / r->divide;
        ssample -= delta;
    }
    /* "2 muestras de latencia" */
    RingReader rr = { ring, (int64_t)ssample - 4 * (int64_t)r->divide, r->divide };
    phase <<= 12;

    float s0 = reader_next(&rr);
    float s1 = reader_next(&rr);
    float s2 = reader_next(&rr);
    float s3 = reader_next(&rr);

    for (uint32_t sample = 0; sample != samples; sample++) {
        uint32_t cphase = phase >> 12;
        *d++ += gain * (- s0 * interp[0][0x1000-cphase] + s1 * interp[1][0x1000-cphase] + s2 * interp[1][cphase] - s3 * interp[0][cphase]);

        phase += r->step;
        if (phase & 0x1000000) {
            phase &= 0xffffff;
            s0 = s1;
            s1 = s2;
            s2 = s3;
            s3 = reader_next(&rr);
        }
    }
}

/* ---- flujos de los chips -------------------------------------------------------------------- */
static inline uint64_t stream_index(uint64_t t, uint32_t rate) {
    return t * rate / SOUND_Z80_HZ;          /* floor(t[s] * rate), t en ciclos del Z80 */
}

static void ym_update(Cps1Sound *s, uint64_t upto) {     /* genera hasta la muestra `upto` inclusive */
    if (upto + 1 <= s->ym_count)
        return;
    uint64_t n = upto + 1 - s->ym_count;
    int32_t buf[2 * 256];
    while (n) {
        unsigned k = n < 256 ? (unsigned)n : 256;
        ym2151_generate(s->ym, buf, k);
        float f[2 * 256];
        for (unsigned i = 0; i < k; i++) {
            uint64_t idx = (s->ym_count + i) & (SOUND_RING - 1);
            /* put_int(..., 32768): double(muestra) / 32768 guardado como float */
            s->ym_l[idx] = f[2 * i] = (float)((double)buf[2 * i] / 32768);
            s->ym_r[idx] = f[2 * i + 1] = (float)((double)buf[2 * i + 1] / 32768);
        }
        if (s->f_ym) fwrite(f, sizeof(float), 2 * k, s->f_ym);
        s->ym_count += k;
        n -= k;
    }
}

static void oki_update(Cps1Sound *s, uint64_t upto) {
    if (upto + 1 <= s->oki_count)
        return;
    uint64_t n = upto + 1 - s->oki_count;
    while (n--) {
        float v = oki6295_generate(&s->oki);
        s->oki_buf[s->oki_count & (SOUND_RING - 1)] = v;
        if (s->f_oki) fwrite(&v, sizeof v, 1, s->f_oki);
        s->oki_count++;
    }
}

/* actualización periódica k (instante k / 50 s): flujos de los chips y bloque nuevo del altavoz */
static void periodic_update(Cps1Sound *s, uint64_t k) {
    ym_update(s, k * SOUND_YM_RATE / SOUND_UPDATE_HZ);
    oki_update(s, k * SOUND_OKI_RATE / SOUND_UPDATE_HZ);
    uint64_t last = k * SOUND_OUT_RATE / SOUND_UPDATE_HZ;
    if (last + 1 > s->out_count) {
        uint32_t n = (uint32_t)(last + 1 - s->out_count);
        float *mix = calloc(n, sizeof *mix);
        lofi_apply(&s->rs_ym, s->ym_l, mix, s->out_count, s->gain_ym, n);
        lofi_apply(&s->rs_ym, s->ym_r, mix, s->out_count, s->gain_ym, n);
        lofi_apply(&s->rs_oki, s->oki_buf, mix, s->out_count, s->gain_oki, n);
        if (s->f_mono) fwrite(mix, sizeof *mix, n, s->f_mono);
        if (s->on_audio) s->on_audio(s->user, mix, n);
        s->out_count += n;
        free(mix);
        if (s->f_chunks)
            fprintf(s->f_chunks, "%llu %llu %u\n", (unsigned long long)(s->ym_count - s->chunk_ym0),
                    (unsigned long long)(s->oki_count - s->chunk_oki0), n);
    }
    s->chunk_ym0 = s->ym_count;
    s->chunk_oki0 = s->oki_count;
    s->updates = k;
}

/* ---- sucesos -----------------------------------------------------------------------------------
 * Todos en ciclos enteros del Z80.  La actualización periódica k vence en k * 3579545 / 50 = k * 71590,9
 * ciclos, entre dos ciclos; cuenta como hecha antes de cualquier acceso desde floor(k * 71590,9): MAME hace
 * correr el Z80 hasta ese ciclo entero, lo para al final del ciclo M que lo alcanza y ejecuta el
 * temporizador de la actualización antes del siguiente ciclo M (comprobado contra MAME: un comando del OKI
 * escrito en el ciclo 188856794, con la actualización en 188856794,2, se aplica después de ella). */
#define DECI_PER_UPDATE (SOUND_Z80_HZ * 10u / SOUND_UPDATE_HZ)     /* 715909 décimas de ciclo */

static inline uint64_t update_cycle(uint64_t k) {
    return k * DECI_PER_UPDATE / 10;
}

static void process_events(Cps1Sound *s, uint64_t t) {
    for (;;) {
        uint64_t t_up = update_cycle(s->updates + 1);
        uint64_t t_ym = ym2151_next_event(s->ym);
        uint64_t t_l = s->lq_count ? s->lq[s->lq_head].t : UINT64_MAX;
        uint64_t best = t_up < t_ym ? t_up : t_ym;
        if (t_l < best) best = t_l;
        if (best > t)
            return;
        if (best == t_l) {
            SoundLatchWrite *w = &s->lq[s->lq_head];
            s->latch[w->which - 1] = w->val;
            s->lq_head = (s->lq_head + 1) % SOUND_LATCH_QUEUE;
            s->lq_count--;
        } else if (best == t_ym) {
            ym2151_run_event(s->ym);
        } else {
            periodic_update(s, s->updates + 1);
        }
    }
}

/* ---- bus del Z80 ------------------------------------------------------------------------------ */
static uint8_t io_read(Cps1Sound *s, uint16_t a, uint64_t t) {
    process_events(s, t);
    uint8_t v;
    switch (a) {
    case 0xf000: case 0xf001:
        ym_update(s, stream_index(t, SOUND_YM_RATE));
        v = ym2151_read(s->ym, t, t, a & 1);
        s->ym_reads++;
        break;
    case 0xf002:
        oki_update(s, stream_index(t, SOUND_OKI_RATE));
        v = oki6295_read(&s->oki);
        s->oki_reads++;
        break;
    case 0xf008: v = s->latch[0]; s->latch_reads++; break;
    case 0xf00a: v = s->latch[1]; s->latch_reads++; break;
    default: v = 0; s->unmapped++; break;
    }
    if (s->f_events) fprintf(s->f_events, "R %04x %02x %llu\n", a, v, (unsigned long long)t);
    return v;
}

static void io_write(Cps1Sound *s, uint16_t a, uint8_t v, uint64_t t) {
    process_events(s, t);
    if (s->f_events) fprintf(s->f_events, "W %04x %02x %llu\n", a, v, (unsigned long long)t);
    switch (a) {
    case 0xf000: case 0xf001:
        ym_update(s, stream_index(t, SOUND_YM_RATE));
        ym2151_write(s->ym, t, t, a & 1, v);
        s->ym_writes++;
        break;
    case 0xf002:
        oki_update(s, stream_index(t, SOUND_OKI_RATE));
        oki6295_write(&s->oki, v);
        s->oki_writes++;
        break;
    case 0xf004: s->bank = v & 1; s->bank_writes++; break;
    case 0xf006:
        s->pin7_writes++;
        if (!(v & 1))
            s->warned_pin7 = 1;      /* SF2 no lo hace; cambiaría la frecuencia del OKI a 1 MHz / 165 (lo informa el anfitrión) */
        s->oki.pin7 = v & 1;
        break;
    default: s->unmapped++; break;
    }
}

static uint8_t bus_read(void *ctx, uint16_t a, uint64_t t) {
    Cps1Sound *s = ctx;
    if (a < 0x8000) return s->rom[a];
    if (a < 0xc000) return s->rom[0x10000 + s->bank * 0x4000 + (a - 0x8000)];
    if (a >= 0xd000 && a < 0xd800) return s->ram[a - 0xd000];
    if (a >= 0xf000 && a < 0xf010) return io_read(s, a, t);
    s->unmapped++;
    return 0;
}

static void bus_write(void *ctx, uint16_t a, uint8_t v, uint64_t t) {
    Cps1Sound *s = ctx;
    if (a >= 0xd000 && a < 0xd800) { s->ram[a - 0xd000] = v; return; }
    if (a >= 0xf000 && a < 0xf010) { io_write(s, a, v, t); return; }
    if (a >= 0xc000) s->unmapped++;      /* las escrituras en ROM se ignoran sin contarlas */
}

/* ---- API ------------------------------------------------------------------------------------- */
int sound_init(Cps1Sound *s, const RomSet *rs) {
    memset(s, 0, sizeof *s);
    s->trace_from = UINT64_MAX;
    for (int i = 0; i < ROMSET_COUNT; i++) {
        const RomInfo *ri = &romset_catalogue[i];
        if (ri->kind != ROMKIND_Z80 && ri->kind != ROMKIND_OKI)
            continue;
        if (!rs->data[i])
            return -1;
        if (ri->kind == ROMKIND_Z80) {
            /* ROM_LOAD(0x00000, 0x8000) + ROM_CONTINUE(0x10000, 0x8000) */
            memcpy(s->rom, rs->data[i], 0x8000);
            memcpy(s->rom + 0x10000, rs->data[i] + 0x8000, 0x8000);
        } else {
            memcpy(s->okirom + ri->dest, rs->data[i], ri->size);
        }
    }
    if (!interp_ready) interp_init();

    s->ym = ym2151_new();
    if (!s->ym) return -1;
    ym2151_reset(s->ym);
    oki6295_init(&s->oki, s->okirom);
    s->z.bus = (Z80Bus){ s, bus_read, bus_read, bus_write, NULL, NULL };
    z80_reset(&s->z);
    lofi_init(&s->rs_ym, SOUND_YM_RATE, SOUND_OUT_RATE);
    lofi_init(&s->rs_oki, SOUND_OKI_RATE, SOUND_OUT_RATE);
    s->gain_ym = 0.35f;
    s->gain_oki = 0.30f;
    return 0;
}

void sound_reset(Cps1Sound *s) {
    z80_reset(&s->z);
    memset(s->ram, 0, sizeof s->ram);
    s->bank = 0;
    s->latch[0] = s->latch[1] = 0;
    ym2151_reset(s->ym);
    oki6295_init(&s->oki, s->okirom);
    s->lq_head = s->lq_count = 0;
    s->ym_count = s->oki_count = s->out_count = s->updates = 0;
    memset(s->ym_l, 0, sizeof s->ym_l);
    memset(s->ym_r, 0, sizeof s->ym_r);
    memset(s->oki_buf, 0, sizeof s->oki_buf);
    s->chunk_ym0 = s->chunk_oki0 = 0;
    s->latch_writes = s->latch_reads = s->ym_writes = s->ym_reads = s->oki_writes = s->oki_reads = s->unmapped = 0;
    s->pin7_writes = s->bank_writes = 0;
    s->warned_pin7 = 0;
}

void sound_state(Cps1Sound *s, StateStream *st) {
    z80_state(&s->z, st);
    st_bytes(st, s->ram, sizeof s->ram);
    int32_t bank = s->bank;
    st_i32(st, &bank);
    st_check(st, bank == 0 || bank == 1);
    s->bank = bank;
    st_u8(st, &s->latch[0]);
    st_u8(st, &s->latch[1]);
    ym2151_state(s->ym, st);
    oki6295_state(&s->oki, st);
    uint32_t head = s->lq_head, count = s->lq_count;
    st_u32(st, &head);
    st_u32(st, &count);
    st_check(st, head < SOUND_LATCH_QUEUE && count <= SOUND_LATCH_QUEUE);
    s->lq_head = head;
    s->lq_count = count;
    for (unsigned i = 0; i < SOUND_LATCH_QUEUE; i++) {
        SoundLatchWrite *w = &s->lq[i];
        st_u64(st, &w->t);
        st_u8(st, &w->which);
        st_u8(st, &w->val);
    }
    for (unsigned k = 0; k < s->lq_count && k < SOUND_LATCH_QUEUE; k++) {      /* las entradas pendientes indexan latch[] */
        unsigned w = s->lq[(s->lq_head + k) % SOUND_LATCH_QUEUE].which;
        st_check(st, w == 1 || w == 2);
    }
    st_u64(st, &s->ym_count);
    st_u64(st, &s->oki_count);
    st_u64(st, &s->out_count);
    st_u64(st, &s->updates);
    st_f32s(st, s->ym_l, SOUND_RING);
    st_f32s(st, s->ym_r, SOUND_RING);
    st_f32s(st, s->oki_buf, SOUND_RING);
}

void sound_free(Cps1Sound *s) {
    ym2151_free(s->ym);
    s->ym = NULL;
}

void sound_latch_write(Cps1Sound *s, int which, uint8_t val, uint64_t cyc68) {
    s->latch_writes++;
    uint64_t tz = sound_z80_cycle(cyc68);
    if (s->f_events)
        fprintf(s->f_events, "L %d %02x %llu %llu\n", which, val, (unsigned long long)cyc68, (unsigned long long)tz);
    if (s->lq_count == SOUND_LATCH_QUEUE) {
        /* el Z80 va muy por detrás: aplicar la más antigua ya */
        process_events(s, s->lq[s->lq_head].t);
    }
    unsigned i = (s->lq_head + s->lq_count) % SOUND_LATCH_QUEUE;
    s->lq[i] = (SoundLatchWrite){ tz, (uint8_t)which, val };
    s->lq_count++;
}

/* traza de depuración (sound_set_trace): cada paso del Z80 en el rango de ciclos, a s->f_trace */
static uint8_t trace_peek(void *ctx, uint16_t a) {
    Cps1Sound *s = ctx;
    if (a < 0x8000) return s->rom[a];
    if (a < 0xc000) return s->rom[0x10000 + s->bank * 0x4000 + (a - 0x8000)];
    if (a >= 0xd000 && a < 0xd800) return s->ram[a - 0xd000];
    return 0;
}

static void trace_step(Cps1Sound *s) {
    Z80 *z = &s->z;
    char buf[64];
    if (z->irq_line && z->iff1 && !z->after_ei)
        snprintf(buf, sizeof buf, "<aceptacion de INT>");
    else if (z->halt)
        snprintf(buf, sizeof buf, "<halt>");
    else
        z80_disasm(z->pc, trace_peek, s, buf, sizeof buf);
    uint64_t ev = ym2151_next_event(s->ym);
    fprintf(s->f_trace, "z80 %10llu %04x %-22s af=%04x bc=%04x de=%04x hl=%04x sp=%04x int=%d iff=%d ym_suceso=%lld\n",
            (unsigned long long)z->cycles, z->pc, buf, z->af.w, z->bc.w, z->de.w, z->hl.w, z->sp, z->irq_line, z->iff1,
            ev == YM2151_NEVER ? -1ll : (long long)ev);
}

void sound_set_trace(Cps1Sound *s, FILE *f, uint64_t from, uint64_t to) {
    s->f_trace = f;
    s->trace_from = f ? from : UINT64_MAX;
    s->trace_to = to;
}

void sound_run_until(Cps1Sound *s, uint64_t cyc68) {
    uint64_t target = sound_z80_cycle(cyc68);
    Z80 *z = &s->z;
    while (z->cycles < target) {
        process_events(s, z->cycles);
        z80_set_irq(z, ym2151_irq(s->ym));
        if (z->cycles >= s->trace_from && z->cycles <= s->trace_to)
            trace_step(s);
        z80_step(z);
    }
}

void sound_report(const Cps1Sound *s, FILE *f) {
    fprintf(f, "sonido: Z80 %llu ciclos, %llu instrucciones, %llu interrupciones; latch: %llu escrituras del 68000, "
            "%llu lecturas del Z80; YM2151: %llu escrituras, %llu lecturas; OKI: %llu escrituras, %llu lecturas, "
            "%llu frases, %llu paradas, %llu rechazadas; %llu accesos sin mapear\n",
            (unsigned long long)s->z.cycles, (unsigned long long)s->z.insns, (unsigned long long)s->z.irqs,
            (unsigned long long)s->latch_writes, (unsigned long long)s->latch_reads,
            (unsigned long long)s->ym_writes, (unsigned long long)s->ym_reads,
            (unsigned long long)s->oki_writes, (unsigned long long)s->oki_reads,
            (unsigned long long)s->oki.starts, (unsigned long long)s->oki.stops, (unsigned long long)s->oki.rejected,
            (unsigned long long)s->unmapped);
    fprintf(f, "sonido: muestras YM2151 %llu, OKI %llu, salida %llu (48 kHz, %.2f s)\n", (unsigned long long)s->ym_count,
            (unsigned long long)s->oki_count, (unsigned long long)s->out_count, s->out_count / (double)SOUND_OUT_RATE);
}
