#include "mem.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

void mem_init(Mem *m, const uint8_t *rom, const char *name) {
    memset(m, 0, sizeof *m);
    m->rom = rom;
    m->gfxram = calloc(GFXRAM_SIZE, 1);
    m->wram = calloc(WRAM_SIZE, 1);
    m->name = name;
    m->in0 = 0xff;     /* monedas / start / servicio: nada pulsado */
    m->in1 = 0xffff;   /* joysticks y botones 1-3 de ambos jugadores */
    m->in2 = 0xff;     /* botones 4-6 (puerto de 8 bits en la placa C) */
    m->dswa = MEM_DSWA_DEFAULT;
    m->dswb = MEM_DSWB_DEFAULT;
    m->dswc = MEM_DSWC_DEFAULT;
    m->coinctrl = 0;   /* al encender, los bits de bloqueo valen 0: monedas bloqueadas hasta que el juego escriba 0x0C */
    mem_latch_inputs(m);
}

void mem_free(Mem *m) {
    free(m->gfxram);
    free(m->wram);
    m->gfxram = m->wram = NULL;
}

void mem_reset(Mem *m) {
    memset(m->gfxram, 0, GFXRAM_SIZE);
    memset(m->wram, 0, WRAM_SIZE);
    memset(m->cps_a, 0, sizeof m->cps_a);
    memset(m->cps_b, 0, sizeof m->cps_b);
    memset(m->palette, 0, sizeof m->palette);
    m->palette_copies = 0;
    m->soundlatch = m->soundlatch2 = 0;
    m->coinctrl = 0;
    m->coin_counter[0] = m->coin_counter[1] = 0;
    m->sound_writes = 0;
    m->in0 = 0xff;
    m->in1 = 0xffff;
    m->in2 = 0xff;
    m->io_reads = m->io_writes = m->unmapped = 0;
    memset(m->io, 0, sizeof m->io);
    m->nlog = 0;
    mem_latch_inputs(m);
}

void mem_state(Mem *m, StateStream *s) {
    st_bytes(s, m->gfxram, GFXRAM_SIZE);
    st_bytes(s, m->wram, WRAM_SIZE);
    st_u16s(s, m->cps_a, sizeof m->cps_a / sizeof m->cps_a[0]);
    st_u16s(s, m->cps_b, sizeof m->cps_b / sizeof m->cps_b[0]);
    st_u16s(s, m->palette, PALETTE_ENTRIES);
    st_u16(s, &m->soundlatch);
    st_u16(s, &m->soundlatch2);
    st_u16(s, &m->coinctrl);
    st_u64(s, &m->coin_counter[0]);
    st_u64(s, &m->coin_counter[1]);
    st_u16(s, &m->in0_live);
    st_u16(s, &m->in1_live);
    st_u16(s, &m->in2_live);
}

void mem_reset_log(Mem *m) { m->nlog = 0; }

/* MAME (ioport_field::frame_update): una moneda pulsada se ignora si su salida de bloqueo está activa.
 * cps1_coinctrl_w: coin_lockout_w(n, BIT(~data, 10 + n)), es decir bloqueo activo cuando el bit vale 0. */
void mem_latch_inputs(Mem *m) {
    uint16_t in0 = m->in0 & 0xff;
    for (int n = 0; n < 2; n++)
        if (!((m->coinctrl >> (10 + n)) & 1))
            in0 |= (uint16_t)(1u << n);      /* bloqueada: se lee como no pulsada */
    m->in0_live = in0;
    m->in1_live = m->in1;
    m->in2_live = m->in2 & 0xff;
}

void mem_copy_inputs(Mem *dst, const Mem *src) {
    dst->in0 = src->in0; dst->in1 = src->in1; dst->in2 = src->in2;
    dst->in0_live = src->in0_live; dst->in1_live = src->in1_live; dst->in2_live = src->in2_live;
}

static inline void log_write(Mem *m, uint32_t addr, uint32_t val) {
    if (m->nlog < WLOG_MAX) {
        m->wlog[m->nlog].addr = addr;
        m->wlog[m->nlog].val = (uint16_t)val;
        m->nlog++;
    }
}

/* ---- E/S ------------------------------------------------------------------------------- */
static uint32_t io_read16(Mem *m, uint32_t off) {
    uint32_t v;
    if (off < 0x08) {
        v = m->in1_live;
    } else if (off >= 0x18 && off < 0x20) {
        uint32_t in;
        switch ((off >> 1) & 3) {
        case 0: in = m->in0_live; break;
        case 1: in = m->dswa; break;
        case 2: in = m->dswb; break;
        default: in = m->dswc; break;
        }
        v = ((in & 0xff) << 8) | 0xff;
    } else if (off >= 0x140 && off < 0x180) {
        uint32_t r = off - 0x140;
        if (r == CPSB_ID_OFFSET)
            v = CPSB_ID_VALUE;
        else if (r == CPSB_IN2_OFFSET)
            v = m->in2_live & 0xff;              /* puerto de 8 bits: el byte alto lee 0 */
        else if (r == 0)
            v = (uint32_t)(m->cps_b[0] * m->cps_b[0]) & 0xffff;   /* resto del multiplicador de otros CPS-B en MAME */
        else
            v = 0xffff;                          /* cps1_cps_b_r: registros no leíbles */
    } else {
        v = 0;                                   /* zonas de solo escritura y 0x800020: valor sin mapear de MAME */
    }
    m->io_reads++;
    m->io[off >> 1].reads++;
    m->io[off >> 1].last_read = (uint16_t)v;
    if (m->log_io && m->log_file)
        fprintf(m->log_file, "[%s] io rd16 %06x -> %04x\n", m->name, IO_BASE + off, v);
    return v;
}

/* Copia de la paleta de la RAM gráfica a la RAM de paleta (cps1_build_palette de MAME): se hace en cada
 * escritura de la base de paleta, con el valor del control de paleta en ese instante.  La base es
 * reg * 256 alineada a 0x400 (m_palette_align) dentro de los 0x40000 de la RAM gráfica.  Las páginas
 * no habilitadas se saltan en la RAM gráfica solo si ya se copió alguna página antes (peculiaridad
 * comprobada en placa por MAME).  Lecturas fuera de la RAM gráfica (base > 0x30000) devuelven 0. */
void mem_palette_copy(Mem *m) {
    uint32_t base = ((uint32_t)m->cps_a[CPSA_PALETTE_BASE] * 256u) & ~0x3ffu & 0x3ffffu;
    uint32_t src = base;
    uint16_t ctrl = m->cps_b[CPSB11_PALETTE_CONTROL];
    for (int page = 0; page < PALETTE_PAGES; page++) {
        if ((ctrl >> page) & 1) {
            for (int i = 0; i < 0x200; i++, src += 2) {
                uint16_t v = 0;
                if (src + 1 < GFXRAM_SIZE)
                    v = (uint16_t)((m->gfxram[src] << 8) | m->gfxram[src + 1]);
                m->palette[page * 0x200 + i] = v;
            }
        } else if (src != base) {
            src += 0x400;
        }
    }
    m->palette_copies++;
}

/* `mask` indica las mitades del bus que escribe la CPU (0xFFFF palabra, 0xFF00 byte par, 0x00FF byte impar),
 * como mem_mask en MAME; los registros aplican COMBINE_DATA. */
static void io_write16(Mem *m, uint32_t off, uint32_t val, uint32_t mask) {
    val &= 0xffff;
    m->io_writes++;
    m->io[off >> 1].writes++;
    m->io[off >> 1].last_write = (uint16_t)val;
    if (off >= 0x100 && off < 0x140) {
        uint16_t *r = &m->cps_a[(off - 0x100) >> 1];
        *r = (uint16_t)((*r & ~mask) | (val & mask));
        if (((off - 0x100) >> 1) == CPSA_PALETTE_BASE)
            mem_palette_copy(m);
    } else if (off >= 0x140 && off < 0x180) {
        uint16_t *r = &m->cps_b[(off - 0x140) >> 1];
        *r = (uint16_t)((*r & ~mask) | (val & mask));
    } else if (off >= 0x180 && off < 0x188) {
        /* cps1_soundlatch_w: el byte bajo si se escribe, si no el byte alto */
        uint8_t v = (uint8_t)((mask & 0x00ff) ? val : val >> 8);
        m->soundlatch = v;
        m->sound_writes++;
        if (m->sound_latch_cb) m->sound_latch_cb(m->sound_ctx, 1, v);
    } else if (off >= 0x188 && off < 0x190) {
        if (mask & 0x00ff) {                     /* cps1_soundlatch2_w: solo el byte bajo */
            m->soundlatch2 = (uint16_t)(val & 0xff);
            if (m->sound_latch_cb) m->sound_latch_cb(m->sound_ctx, 2, (uint8_t)val);
        }
    } else if (off >= 0x30 && off < 0x38) {
        if (mask & 0xff00) {
            for (int n = 0; n < 2; n++)
                if (((val >> (8 + n)) & 1) && !((m->coinctrl >> (8 + n)) & 1))
                    m->coin_counter[n]++;
            m->coinctrl = (uint16_t)val;
        }
    }
    if (m->log_io && m->log_file)
        fprintf(m->log_file, "[%s] io wr16 %06x <- %04x (mascara %04x)\n", m->name, IO_BASE + off, val, mask);
}

/* ---- bus ------------------------------------------------------------------------------- */
/* Acceso de 16 bits alineado (el bus del 68000 es de 16 bits). */
static inline uint32_t bus_read16(Mem *m, uint32_t a) {
    if (a < ROM_SIZE)
        return (m->rom[a] << 8) | m->rom[a + 1];
    if (a >= WRAM_BASE)
        return (m->wram[a - WRAM_BASE] << 8) | m->wram[(a - WRAM_BASE + 1) & 0xffff];
    if (a >= GFXRAM_BASE && a < GFXRAM_BASE + GFXRAM_SIZE)
        return (m->gfxram[a - GFXRAM_BASE] << 8) | m->gfxram[a - GFXRAM_BASE + 1];
    if (a >= IO_BASE && a < IO_BASE + IO_SIZE)
        return io_read16(m, a - IO_BASE);
    m->unmapped++;
    if (m->log_io && m->log_file && m->unmapped < 50)
        fprintf(m->log_file, "[%s] lectura sin mapear %06x -> 0000\n", m->name, a);
    return 0;
}

static inline void bus_write16(Mem *m, uint32_t a, uint32_t v, uint32_t mask) {
    log_write(m, a, v);
    if (a >= WRAM_BASE) {
        m->wram[a - WRAM_BASE] = (uint8_t)(v >> 8);
        m->wram[(a - WRAM_BASE + 1) & 0xffff] = (uint8_t)v;
        return;
    }
    if (a >= GFXRAM_BASE && a < GFXRAM_BASE + GFXRAM_SIZE) {
        m->gfxram[a - GFXRAM_BASE] = (uint8_t)(v >> 8);
        m->gfxram[a - GFXRAM_BASE + 1] = (uint8_t)v;
        return;
    }
    if (a >= IO_BASE && a < IO_BASE + IO_SIZE) {
        io_write16(m, a - IO_BASE, v, mask);
        return;
    }
    m->unmapped++;
    if (m->log_io && m->log_file && m->unmapped < 50)
        fprintf(m->log_file, "[%s] escritura %s %06x <- %04x\n", m->name, a < ROM_SIZE ? "en ROM" : "sin mapear", a, v);
}

uint32_t mem_read16(Mem *m, uint32_t addr) {
    addr &= 0xffffff;
    if (addr & 1) {   /* acceso desalineado: el 68000 real daría error de dirección; componemos bytes */
        return (mem_read8(m, addr) << 8) | mem_read8(m, addr + 1);
    }
    return bus_read16(m, addr);
}

uint32_t mem_read8(Mem *m, uint32_t addr) {
    addr &= 0xffffff;
    uint32_t w = bus_read16(m, addr & ~1u);
    return (addr & 1) ? (w & 0xff) : (w >> 8);
}

uint32_t mem_read32(Mem *m, uint32_t addr) {
    return (mem_read16(m, addr) << 16) | mem_read16(m, addr + 2);
}

void mem_write16(Mem *m, uint32_t addr, uint32_t val) {
    addr &= 0xffffff;
    val &= 0xffff;
    if (addr & 1) {
        mem_write8(m, addr, val >> 8);
        mem_write8(m, addr + 1, val & 0xff);
        return;
    }
    bus_write16(m, addr, val, 0xffff);
}

void mem_write8(Mem *m, uint32_t addr, uint32_t val) {
    addr &= 0xffffff;
    val &= 0xff;
    uint32_t a = addr & ~1u;
    if (a >= WRAM_BASE) {
        m->wram[addr - WRAM_BASE] = (uint8_t)val;
        log_write(m, addr, val);
        return;
    }
    if (a >= GFXRAM_BASE && a < GFXRAM_BASE + GFXRAM_SIZE) {
        m->gfxram[addr - GFXRAM_BASE] = (uint8_t)val;
        log_write(m, addr, val);
        return;
    }
    /* E/S u otras zonas: el 68000 pone el byte en las dos mitades del bus y la máscara indica cuál vale */
    bus_write16(m, a, (val << 8) | val, (addr & 1) ? 0x00ff : 0xff00);
}

void mem_write32(Mem *m, uint32_t addr, uint32_t val) {
    mem_write16(m, addr, val >> 16);
    mem_write16(m, addr + 2, val & 0xffff);
}

int64_t mem_compare(const Mem *a, const Mem *b) {
    for (uint32_t i = 0; i < WRAM_SIZE; i++)
        if (a->wram[i] != b->wram[i])
            return WRAM_BASE + i;
    for (uint32_t i = 0; i < GFXRAM_SIZE; i++)
        if (a->gfxram[i] != b->gfxram[i])
            return GFXRAM_BASE + i;
    if (memcmp(a->cps_a, b->cps_a, sizeof a->cps_a))
        return IO_BASE + 0x100;
    if (memcmp(a->cps_b, b->cps_b, sizeof a->cps_b))
        return IO_BASE + 0x140;
    if (memcmp(a->palette, b->palette, sizeof a->palette))
        return IO_BASE + 0x10a;
    if (a->soundlatch != b->soundlatch)
        return IO_BASE + 0x180;
    return -1;
}

/* ---- tabla de E/S ---------------------------------------------------------------------- */
static const struct { uint32_t addr; const char *name; } io_names[] = {
    { 0x800000, "IN1: joysticks y botones 1-3 (P1 byte alto, P2 byte bajo), activo a nivel bajo" },
    { 0x800018, "IN0 (byte alto): monedas, servicio, start 1/2, modo test" },
    { 0x80001a, "DSWA (byte alto): precio" },
    { 0x80001c, "DSWB (byte alto): dificultad" },
    { 0x80001e, "DSWC (byte alto): free play, freeze, flip, sonido en demo, continuar, modo juego/test" },
    { 0x800020, "sin función (nopr en MAME), lee 0" },
    { 0x800030, "control de monedas (byte alto): bits 8-9 contadores, bits 10-11 bloqueo (1 = acepta)" },
    { 0x800100, "CPS-A: base de OBJ (sprites) en RAM gráfica (>> 8 = dirección / 256)" },
    { 0x800102, "CPS-A: base de SCROLL1 (texto 8x8)" },
    { 0x800104, "CPS-A: base de SCROLL2 (16x16)" },
    { 0x800106, "CPS-A: base de SCROLL3 (32x32)" },
    { 0x800108, "CPS-A: base de la zona OTHER (row scroll)" },
    { 0x80010a, "CPS-A: base de la paleta" },
    { 0x80010c, "CPS-A: scroll X de SCROLL1" },
    { 0x80010e, "CPS-A: scroll Y de SCROLL1" },
    { 0x800110, "CPS-A: scroll X de SCROLL2" },
    { 0x800112, "CPS-A: scroll Y de SCROLL2" },
    { 0x800114, "CPS-A: scroll X de SCROLL3" },
    { 0x800116, "CPS-A: scroll Y de SCROLL3" },
    { 0x800118, "CPS-A: scroll X de estrellas 1 (no usado por SF2)" },
    { 0x80011a, "CPS-A: scroll Y de estrellas 1" },
    { 0x80011c, "CPS-A: scroll X de estrellas 2" },
    { 0x80011e, "CPS-A: scroll Y de estrellas 2" },
    { 0x800120, "CPS-A: offset del row scroll" },
    { 0x800122, "CPS-A: control de vídeo (flip screen, row scroll, bancos)" },
    { 0x800166, "CPS-B-11: control de capas (habilitación y orden)" },
    { 0x800168, "CPS-B-11: máscara de prioridad 1" },
    { 0x80016a, "CPS-B-11: máscara de prioridad 2" },
    { 0x80016c, "CPS-B-11: máscara de prioridad 3" },
    { 0x80016e, "CPS-B-11: máscara de prioridad 4" },
    { 0x800170, "CPS-B-11: control de paleta (páginas habilitadas)" },
    { 0x800172, "CPS-B-11: ID del chip, lee 0x0401 (comprobación del juego en 0x4C2 y en cada IRQ2)" },
    { 0x800176, "CPS-B-11: IN2 (byte bajo): botones 4-6 (patadas) de P1 (bits 0-2) y P2 (bits 4-6)" },
    { 0x800180, "latch de sonido (byte bajo): comando al Z80" },
    { 0x800188, "latch 2 de sonido (byte bajo): fade / temporizador" },
};

const char *mem_io_name(uint32_t addr) {
    addr &= ~1u;
    for (size_t i = 0; i < sizeof io_names / sizeof io_names[0]; i++)
        if (io_names[i].addr == addr)
            return io_names[i].name;
    if (addr >= 0x800000 && addr < 0x800008) return "IN1 (espejo)";
    if (addr >= 0x800030 && addr < 0x800038) return "control de monedas (espejo)";
    if (addr >= 0x800100 && addr < 0x800140) return "CPS-A: registro sin uso conocido";
    if (addr >= 0x800140 && addr < 0x800180) return "CPS-B-11: registro sin uso conocido (lee 0xFFFF)";
    if (addr >= 0x800180 && addr < 0x800188) return "latch de sonido (espejo)";
    if (addr >= 0x800188 && addr < 0x800190) return "latch 2 de sonido (espejo)";
    return "";
}

void mem_print_io_table(const Mem *m, FILE *f) {
    fprintf(f, "| Dirección | Lecturas | Escrituras | Última lectura | Última escritura | Función |\n");
    fprintf(f, "|---|---:|---:|---|---|---|\n");
    for (uint32_t i = 0; i < IO_WORDS; i++) {
        const IoStat *s = &m->io[i];
        if (!s->reads && !s->writes)
            continue;
        uint32_t addr = IO_BASE + i * 2;
        char rd[8] = "-", wr[8] = "-";
        if (s->reads) snprintf(rd, sizeof rd, "0x%04X", s->last_read);
        if (s->writes) snprintf(wr, sizeof wr, "0x%04X", s->last_write);
        fprintf(f, "| 0x%06X | %llu | %llu | %s | %s | %s |\n", addr, (unsigned long long)s->reads,
                (unsigned long long)s->writes, rd, wr, mem_io_name(addr));
    }
    fprintf(f, "\nAccesos sin mapear: %llu (leen 0x0000). Pulsos de contador de monedas: %llu / %llu. "
               "Comandos de sonido: %llu.\n", (unsigned long long)m->unmapped, (unsigned long long)m->coin_counter[0],
            (unsigned long long)m->coin_counter[1], (unsigned long long)m->sound_writes);
}
