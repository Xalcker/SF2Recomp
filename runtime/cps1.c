#include "cps1.h"
#include "cpu.h"
#include "state.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Teclas de la secuencia de entradas y su bit en cada puerto (activo a nivel bajo).
 * Mismos nombres en tools/mame_blind.lua. */
static const struct { const char *name; uint8_t port; uint16_t mask; } input_names[] = {
    { "coin1", 0, 0x0001 }, { "coin2", 0, 0x0002 }, { "service", 0, 0x0004 },
    { "start1", 0, 0x0010 }, { "start2", 0, 0x0020 }, { "test", 0, 0x0040 },
    { "p1_right", 1, 0x0001 }, { "p1_left", 1, 0x0002 }, { "p1_down", 1, 0x0004 }, { "p1_up", 1, 0x0008 },
    { "p1_lp", 1, 0x0010 }, { "p1_mp", 1, 0x0020 }, { "p1_hp", 1, 0x0040 },
    { "p2_right", 1, 0x0100 }, { "p2_left", 1, 0x0200 }, { "p2_down", 1, 0x0400 }, { "p2_up", 1, 0x0800 },
    { "p2_lp", 1, 0x1000 }, { "p2_mp", 1, 0x2000 }, { "p2_hp", 1, 0x4000 },
    { "p1_lk", 2, 0x0001 }, { "p1_mk", 2, 0x0002 }, { "p1_hk", 2, 0x0004 },
    { "p2_lk", 2, 0x0010 }, { "p2_mk", 2, 0x0020 }, { "p2_hk", 2, 0x0040 },
};

int cps1_input_lookup(const char *name, uint8_t *port, uint16_t *mask) {
    for (size_t i = 0; i < sizeof input_names / sizeof input_names[0]; i++)
        if (!strcmp(name, input_names[i].name)) {
            *port = input_names[i].port;
            *mask = input_names[i].mask;
            return 1;
        }
    return 0;
}

void cps1_init(Cps1 *m, Mem *mem) {
    memset(m, 0, sizeof *m);
    m->mem = mem;
    m->frame_cycles = CPS1_FRAME_CYCLES;
}

static void perr(char *err, size_t errn, const char *name, int line, const char *msg, const char *arg) {
    if (!err || !errn)
        return;
    size_t len = strlen(err);
    if (len + 1 < errn)
        snprintf(err + len, errn - len, "%s:%d: %s%s%s\n", name, line, msg, arg ? " " : "", arg ? arg : "");
}

int cps1_parse_inputs(Cps1 *m, const char *text, const char *name, char *err, size_t errn) {
    int lineno = 0, cap = 0, bad = 0;
    if (err && errn)
        err[0] = 0;
    free(m->events);
    m->events = NULL;
    m->nevents = 0;
    while (*text) {
        char line[256];
        size_t n = 0;
        while (text[n] && text[n] != '\n')
            n++;
        size_t k = n < sizeof line - 1 ? n : sizeof line - 1;
        memcpy(line, text, k);
        line[k] = 0;
        text += n;
        if (*text == '\n')
            text++;
        lineno++;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0')
            continue;
        char key[64];
        unsigned long long from, to;
        if (sscanf(p, "%llu %llu %63s", &from, &to, key) != 3) {
            perr(err, errn, name, lineno, "formato esperado 'desde hasta tecla'", NULL);
            bad++;
            continue;
        }
        uint8_t port; uint16_t mask;
        if (!cps1_input_lookup(key, &port, &mask)) {
            perr(err, errn, name, lineno, "tecla desconocida", key);
            bad++;
            continue;
        }
        if (to < from) {
            perr(err, errn, name, lineno, "'hasta' menor que 'desde'", NULL);
            bad++;
            continue;
        }
        if (m->nevents == cap) {
            cap = cap ? cap * 2 : 32;
            InputEvent *ne = realloc(m->events, (size_t)cap * sizeof *m->events);
            if (!ne) return -1;
            m->events = ne;
        }
        m->events[m->nevents++] = (InputEvent){ from, to, port, mask };
    }
    return bad ? -2 : m->nevents;
}

void cps1_apply_inputs(Cps1 *m, uint64_t frame) {
    uint16_t in0 = 0xff, in1 = 0xffff, in2 = 0xff;
    for (int i = 0; i < m->nevents; i++) {
        const InputEvent *e = &m->events[i];
        if (frame < e->from || frame > e->to)
            continue;
        switch (e->port) {
        case 0: in0 &= (uint16_t)~e->mask; break;
        case 1: in1 &= (uint16_t)~e->mask; break;
        default: in2 &= (uint16_t)~e->mask; break;
        }
    }
    uint16_t dev = 0;
    for (int i = 0; i < 2; i++)
        if (m->dev_in1[i] && frame <= m->dev_until[i])
            dev |= m->dev_in1[i];
    m->mem->in0 = in0 & (uint16_t)~m->manual_in0;
    m->mem->in1 = in1 & (uint16_t)~m->manual_in1 & (uint16_t)~dev;
    m->mem->in2 = in2 & (uint16_t)~m->manual_in2;
    mem_latch_inputs(m->mem);
}

void cps1_state(Cps1 *m, StateStream *s) {
    st_u64(s, &m->frame);
}

void cps1_on_irq2(Cps1 *m, M68K *c) {
    (void)c;
    m->frame++;
    cps1_apply_inputs(m, m->frame);
}

/* ---- sumas de verificación ------------------------------------------------------------- */
static inline uint32_t fnv_words(uint32_t h, const uint8_t *p, size_t n) {
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t w = ((uint32_t)p[i] << 8) | p[i + 1];
        h = (h ^ w) * 0x01000193u;
    }
    return h;
}
#define FNV_INIT 0x811c9dc5u

static uint32_t hash_wram_range(const Mem *mem, uint32_t lo, uint32_t hi, uint32_t h) {
    /* [lo, hi) en direcciones absolutas, saltando las zonas volátiles */
    uint32_t cuts[][2] = { { CPS1_VOLATILE1_LO, CPS1_VOLATILE1_HI }, { CPS1_VOLATILE2_LO, CPS1_VOLATILE2_HI } };
    uint32_t a = lo;
    while (a < hi) {
        uint32_t next = hi;
        int in_cut = 0;
        for (int i = 0; i < 2; i++) {
            if (a >= cuts[i][0] && a < cuts[i][1]) { in_cut = 1; a = cuts[i][1]; break; }
            if (cuts[i][0] > a && cuts[i][0] < next) next = cuts[i][0];
        }
        if (in_cut) continue;
        h = fnv_words(h, mem->wram + (a - WRAM_BASE), next - a);
        a = next;
    }
    return h;
}

uint32_t cps1_hash_wram(const Mem *mem) {
    return hash_wram_range(mem, WRAM_BASE, WRAM_BASE + WRAM_SIZE, FNV_INIT);
}

uint32_t cps1_hash_wram_volatile(const Mem *mem) {
    uint32_t h = fnv_words(FNV_INIT, mem->wram + (CPS1_VOLATILE1_LO - WRAM_BASE), CPS1_VOLATILE1_HI - CPS1_VOLATILE1_LO);
    return fnv_words(h, mem->wram + (CPS1_VOLATILE2_LO - WRAM_BASE), CPS1_VOLATILE2_HI - CPS1_VOLATILE2_LO);
}

uint32_t cps1_hash_gfx(const Mem *mem) {
    return fnv_words(FNV_INIT, mem->gfxram, GFXRAM_SIZE);
}

void cps1_sample(Cps1 *m, M68K *c) {
    Mem *mem = m->mem;
    if (!m->summary)
        return;
    uint32_t sp = c->a[7];
    uint32_t pushed_sr = mem_read16(mem, sp + 60);
    uint32_t pushed_pc = mem_read32(mem, sp + 62);
    /* las lecturas anteriores no deben contar como accesos del juego (son RAM, no E/S) */
    FILE *f = m->summary;
    fprintf(f, "f=%llu pc=%06x sr=%04x d=", (unsigned long long)m->frame, pushed_pc & 0xffffff, pushed_sr);
    for (int i = 0; i < 8; i++) fprintf(f, "%s%08x", i ? "," : "", c->d[i]);
    fprintf(f, " a=");
    for (int i = 0; i < 8; i++) fprintf(f, "%s%08x", i ? "," : "", c->a[i]);
    fprintf(f, " in=%02x,%04x,%02x fc=%02x w=%08x v=%08x g=%08x", mem->in0_live & 0xff, mem->in1_live & 0xffff,
            mem->in2_live & 0xff, mem->wram[CPS1_FRAME_COUNTER - WRAM_BASE], cps1_hash_wram(mem),
            cps1_hash_wram_volatile(mem), cps1_hash_gfx(mem));
    if (m->detail) {
        fprintf(f, " wb=");
        for (int b = 0; b < CPS1_WRAM_BLOCKS; b++) {
            uint32_t lo = WRAM_BASE + b * (WRAM_SIZE / CPS1_WRAM_BLOCKS);
            fprintf(f, "%s%08x", b ? "," : "", hash_wram_range(mem, lo, lo + WRAM_SIZE / CPS1_WRAM_BLOCKS, FNV_INIT));
        }
        fprintf(f, " gb=");
        for (int b = 0; b < CPS1_GFX_BLOCKS; b++) {
            size_t sz = GFXRAM_SIZE / CPS1_GFX_BLOCKS;
            fprintf(f, "%s%08x", b ? "," : "", fnv_words(FNV_INIT, mem->gfxram + b * sz, sz));
        }
    }
    fputc('\n', f);
    m->samples++;
}
