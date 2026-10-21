/*
 * cpu.h - estado del MC68000 recompilado y primitivas usadas por el código generado.
 *
 * Las banderas se calculan "simple y correcto antes que rápido", siguiendo bit a bit la
 * semántica de Musashi (incluidos los comportamientos "indefinidos" de BCD, ASL, DIV, CHK)
 * para que el arnés de validación en paralelo no reporte falsas divergencias.
 */
#ifndef RECOMP_CPU_H
#define RECOMP_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "sf2_port.h"
#include "mem.h"

typedef struct M68K M68K;
typedef void (*RecompFn)(M68K *c);

struct M68K {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;          /* dirección de la instrucción en curso (se fija con INSN()) */
    uint32_t sp_other;    /* puntero de pila inactivo: USP si s=1, SSP si s=0 */
    uint8_t x, n, z, v, c; /* banderas CCR como 0/1 */
    uint8_t s;            /* bit supervisor */
    uint8_t t;            /* bit trace (no emulado, solo se conserva en SR) */
    uint8_t im;           /* máscara de interrupción 0..7 */
    uint8_t stopped;      /* tras STOP, hasta la siguiente interrupción */
    uint8_t irq_pending;  /* nivel de IRQ pendiente (0 = ninguno) */
    int32_t mod_fixcam;   /* stage completo (D59): 1 = cámara principal fija en el centro de sus topes; solo con mod_wide > 0 */
    int32_t mod_wide;     /* mods de lógica (D58): píxeles extra de pantalla ancha por lado, 0 = juego original; lo leen los parches de recompiler/mod_patches.txt */
    uint16_t ke;          /* punto de muestreo de interrupciones de la última instrucción (gen_c.py, D47) */
    uint64_t icount;      /* instrucciones ejecutadas */
    uint64_t cycles;      /* ciclos estimados (modo autónomo) */
    Mem *mem;
};

/* ---- ganchos del arnés (definidos por el programa anfitrión) --------------------------- */
void rt_hook(M68K *c);                     /* llamado antes de cada instrucción si RECOMP_HOOK */
void rt_fatal(M68K *c, const char *fmt, ...) SF2_NORETURN SF2_PRINTF(2, 3);
void rt_stopped(M68K *c);                  /* la CPU ejecutó STOP: esperar la siguiente IRQ */
RecompFn rt_lookup(uint32_t addr);         /* tabla de despacho dirección -> función (NULL si no existe) */
/* c->pc no está en la tabla de despacho: el anfitrión ejecuta el código ajeno con otro medio (Musashi en
 * harness/blind.c, solo con --fallback) hasta que c->pc vuelva a ser una dirección conocida, o termina con rt_fatal. */
void rt_unknown(M68K *c);
/* despacho: la función de la tabla que contiene c->pc; si no está, rt_unknown y, si tampoco, rt_fatal */
RecompFn rt_dispatch(M68K *c);

#ifdef RECOMP_HOOK
#define INSN(addr) do { c->pc = (addr); rt_hook(c); } while (0)
#else
#define INSN(addr) do { c->pc = (addr); c->icount++; } while (0)
#endif

#ifdef RECOMP_CYCLES
#define CYC(n) (c->cycles += (n))
#else
#define CYC(n) ((void)0)
#endif

/* ---- memoria -------------------------------------------------------------------------- */
static inline uint32_t rd8(M68K *c, uint32_t a)  { return mem_read8(c->mem, a); }
static inline uint32_t rd16(M68K *c, uint32_t a) { return mem_read16(c->mem, a); }
static inline uint32_t rd32(M68K *c, uint32_t a) { return mem_read32(c->mem, a); }
static inline void wr8(M68K *c, uint32_t a, uint32_t v)  { mem_write8(c->mem, a, v); }
static inline void wr16(M68K *c, uint32_t a, uint32_t v) { mem_write16(c->mem, a, v); }
static inline void wr32(M68K *c, uint32_t a, uint32_t v) { mem_write32(c->mem, a, v); }

static inline void push32(M68K *c, uint32_t v) { c->a[7] -= 4; wr32(c, c->a[7], v); }
static inline void push16(M68K *c, uint32_t v) { c->a[7] -= 2; wr16(c, c->a[7], v); }
static inline uint32_t pop32(M68K *c) { uint32_t v = rd32(c, c->a[7]); c->a[7] += 4; return v; }
static inline uint32_t pop16(M68K *c) { uint32_t v = rd16(c, c->a[7]); c->a[7] += 2; return v; }

/* ---- registro de estado --------------------------------------------------------------- */
static inline uint32_t get_ccr(M68K *c) {
    return (c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->c;
}
static inline void set_ccr(M68K *c, uint32_t v) {
    c->x = (v >> 4) & 1; c->n = (v >> 3) & 1; c->z = (v >> 2) & 1; c->v = (v >> 1) & 1; c->c = v & 1;
}
static inline uint32_t get_sr(M68K *c) {
    return (c->t << 15) | (c->s << 13) | (c->im << 8) | get_ccr(c);
}
static inline void set_s(M68K *c, int s) {
    s = !!s;
    if (c->s != s) {
        uint32_t tmp = c->a[7];
        c->a[7] = c->sp_other;
        c->sp_other = tmp;
        c->s = (uint8_t)s;
    }
}
static inline void set_sr(M68K *c, uint32_t v) {
    v &= 0xa71f;
    c->t = (v >> 15) & 1;
    c->im = (v >> 8) & 7;
    set_ccr(c, v);
    set_s(c, (v >> 13) & 1);
}

/* ---- excepciones ---------------------------------------------------------------------- */
/* Entra en la excepción `vec` apilando `pc_push`; deja en c->pc la dirección del manejador. */
static inline void exception(M68K *c, uint32_t vec, uint32_t pc_push) {
    uint32_t sr = get_sr(c);
    c->t = 0;
    set_s(c, 1);
    push32(c, pc_push);
    push16(c, sr);
    c->pc = rd32(c, vec * 4);
}
/* Reloj del código generado (hito 6, D47): 1 = el del 68000 de MAME 0.289, 0 = el de Musashi (lo escribe gen_c.py). */
extern const int recomp_clock_mame;

/* Primer ciclo del manejador de una interrupción por autovector en MAME 0.289 (hito 6, D47).  `t` es el ciclo en que
 * termina la instrucción interrumpida.  La secuencia de excepción dura 46 ciclos más la espera del ciclo de
 * reconocimiento (IACK) con VPA, que se alinea con el reloj E del 68000 (periodo de 10 ciclos: vpa_sync de m68000.cpp
 * espera al siguiente múltiplo de 10 si faltan más de 3 ciclos y al otro si no).  Resultado medido en trazas de MAME:
 * el manejador empieza en el primer ciclo >= t + 50 que es 5 módulo 10, contando desde el reset. */
static inline uint64_t irq_entry_cycle(uint64_t t) {
    uint64_t e = t + 50;
    return e + (15 - e % 10) % 10;
}

static inline void exception_interrupt(M68K *c, uint32_t level) {
    uint32_t sr = get_sr(c);
    uint32_t vec = 24 + level;
    uint32_t pc_push = c->pc;
    c->t = 0;
    set_s(c, 1);
    c->im = (uint8_t)level;
    uint32_t new_pc = rd32(c, vec * 4);
    if (new_pc == 0)
        new_pc = rd32(c, 15 * 4);
    push32(c, pc_push);
    push16(c, sr);
    c->pc = new_pc;
    c->stopped = 0;
#ifdef RECOMP_CYCLES
    if (recomp_clock_mame)
        c->cycles = irq_entry_cycle(c->cycles);
    else
        CYC(44);          /* autovector: m68ki_exception_cycle_table[0][24..31] de Musashi */
#endif
}

/* ---- ciclos dependientes de los datos de MULU/MULS (modelo de Musashi para el 68000) ------ */
static inline uint32_t cyc_mulu(uint32_t x) {
    uint32_t n = 0;
    for (x &= 0xffff; x; x >>= 1)
        if (x & 1) n += 2;
    return n;
}
static inline uint32_t cyc_muls(uint32_t x) {
    uint32_t y = (uint32_t)(int32_t)(int16_t)x, n = 0, f = 0;
    for (; y; y >>= 1)
        if ((y & 1) != f) { n += 2; f = 1 - f; }
    return n;
}
/* MULS del 68000 real y de MAME (hito 6, D47): 38 + 2n con n = parejas 01 o 10 en los 17 bits <ea>:0.  El bucle de Musashi
 * de arriba para en el último 1 y no cuenta su paso a 0 cuando el operando es positivo (1 cuesta 42, no 40). */
static inline uint32_t cyc_muls_68k(uint32_t x) {
    uint32_t y = (x & 0xffffu) << 1, n = 0;
    for (int i = 0; i < 16; i++)
        if (((y >> i) ^ (y >> (i + 1))) & 1) n += 2;
    return n;
}

/* Función recompilada que contiene c->pc (salto indirecto, retorno no estructurado, trampolín).  La busca el
 * anfitrión (rt_dispatch): así puede registrar cada despacho (cobertura, hito 6) y, si la dirección no está en la
 * tabla, ejecutar el código desconocido con otro medio (rt_unknown) o detenerse con un mensaje claro. */
static inline RecompFn dispatch_fn(M68K *c) {
    return rt_dispatch(c);
}

/* Llama a la función recompilada que contiene c->pc (entrada a mitad de función vía switch). */
static inline void dispatch_call(M68K *c) {
    dispatch_fn(c)(c);
}

/* ---- banderas ------------------------------------------------------------------------- */
#define DEF_SIZE_OPS(BITS, MASK, MSB)                                                            \
static inline void nz##BITS(M68K *c, uint32_t r) { r &= MASK; c->n = (r >> (BITS - 1)) & 1; c->z = r == 0; } \
static inline uint32_t op_logic##BITS(M68K *c, uint32_t r) { r &= MASK; nz##BITS(c, r); c->v = 0; c->c = 0; return r; } \
static inline uint32_t op_add##BITS(M68K *c, uint32_t s, uint32_t d) {                           \
    s &= MASK; d &= MASK; uint64_t full = (uint64_t)s + d; uint32_t r = (uint32_t)full & MASK;    \
    nz##BITS(c, r); c->v = ((s ^ r) & (d ^ r)) >> (BITS - 1) & 1; c->c = c->x = (full >> BITS) & 1; return r; } \
static inline uint32_t op_addx##BITS(M68K *c, uint32_t s, uint32_t d) {                          \
    s &= MASK; d &= MASK; uint64_t full = (uint64_t)s + d + c->x; uint32_t r = (uint32_t)full & MASK; \
    c->n = (r >> (BITS - 1)) & 1; if (r) c->z = 0; c->v = ((s ^ r) & (d ^ r)) >> (BITS - 1) & 1;   \
    c->c = c->x = (full >> BITS) & 1; return r; }                                                 \
static inline uint32_t op_sub##BITS(M68K *c, uint32_t s, uint32_t d) {                           \
    s &= MASK; d &= MASK; uint64_t full = (uint64_t)d - s; uint32_t r = (uint32_t)full & MASK;    \
    nz##BITS(c, r); c->v = ((s ^ d) & (r ^ d)) >> (BITS - 1) & 1; c->c = c->x = (full >> BITS) & 1; return r; } \
static inline uint32_t op_subx##BITS(M68K *c, uint32_t s, uint32_t d) {                          \
    s &= MASK; d &= MASK; uint64_t full = (uint64_t)d - s - c->x; uint32_t r = (uint32_t)full & MASK; \
    c->n = (r >> (BITS - 1)) & 1; if (r) c->z = 0; c->v = ((s ^ d) & (r ^ d)) >> (BITS - 1) & 1;   \
    c->c = c->x = (full >> BITS) & 1; return r; }                                                 \
static inline void op_cmp##BITS(M68K *c, uint32_t s, uint32_t d) {                               \
    s &= MASK; d &= MASK; uint64_t full = (uint64_t)d - s; uint32_t r = (uint32_t)full & MASK;    \
    nz##BITS(c, r); c->v = ((s ^ d) & (r ^ d)) >> (BITS - 1) & 1; c->c = (full >> BITS) & 1; }   \
static inline uint32_t op_neg##BITS(M68K *c, uint32_t d) {                                       \
    d &= MASK; uint64_t full = (uint64_t)0 - d; uint32_t r = (uint32_t)full & MASK;               \
    nz##BITS(c, r); c->v = (d & r) >> (BITS - 1) & 1; c->c = c->x = (full >> BITS) & 1; return r; } \
static inline uint32_t op_negx##BITS(M68K *c, uint32_t d) {                                      \
    d &= MASK; uint64_t full = (uint64_t)0 - d - c->x; uint32_t r = (uint32_t)full & MASK;        \
    c->n = (r >> (BITS - 1)) & 1; if (r) c->z = 0; c->v = (d & r) >> (BITS - 1) & 1;               \
    c->c = c->x = (full >> BITS) & 1; return r; }

DEF_SIZE_OPS(8, 0xffu, 0x80u)
DEF_SIZE_OPS(16, 0xffffu, 0x8000u)
DEF_SIZE_OPS(32, 0xffffffffu, 0x80000000u)

/* escritura parcial en Dn */
static inline void setd8(M68K *c, int n, uint32_t v)  { c->d[n] = (c->d[n] & 0xffffff00u) | (v & 0xffu); }
static inline void setd16(M68K *c, int n, uint32_t v) { c->d[n] = (c->d[n] & 0xffff0000u) | (v & 0xffffu); }
static inline void setd32(M68K *c, int n, uint32_t v) { c->d[n] = v; }

static inline uint32_t sext8(uint32_t v)  { return (uint32_t)(int32_t)(int8_t)v; }
static inline uint32_t sext16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* ---- condiciones ---------------------------------------------------------------------- */
static inline int cond(M68K *c, int cc) {
    switch (cc) {
    case 0: return 1;
    case 1: return 0;
    case 2: return !c->c && !c->z;
    case 3: return c->c || c->z;
    case 4: return !c->c;
    case 5: return c->c;
    case 6: return !c->z;
    case 7: return c->z;
    case 8: return !c->v;
    case 9: return c->v;
    case 10: return !c->n;
    case 11: return c->n;
    case 12: return c->n == c->v;
    case 13: return c->n != c->v;
    case 14: return !c->z && c->n == c->v;
    default: return c->z || c->n != c->v;
    }
}

/* ---- multiplicación / división -------------------------------------------------------- */
static inline uint32_t op_mulu(M68K *c, uint32_t s, uint32_t d) {
    uint32_t r = (s & 0xffff) * (d & 0xffff);
    nz32(c, r); c->v = 0; c->c = 0; return r;
}
static inline uint32_t op_muls(M68K *c, uint32_t s, uint32_t d) {
    uint32_t r = (uint32_t)((int32_t)(int16_t)s * (int32_t)(int16_t)d);
    nz32(c, r); c->v = 0; c->c = 0; return r;
}
/* divisor != 0; devuelve el nuevo valor del registro (sin cambios si desborda) */
static inline uint32_t op_divu(M68K *c, uint32_t dst, uint32_t src) {
    src &= 0xffff;
    uint32_t q = dst / src, r = dst % src;
    if (q < 0x10000) {
        c->z = q == 0; c->n = (q >> 15) & 1; c->v = 0; c->c = 0;
        return (q & 0xffff) | (r << 16);
    }
    c->v = 1;
    return dst;
}
static inline uint32_t op_divs(M68K *c, uint32_t dst, uint32_t src) {
    int32_t s = (int16_t)src;
    if (dst == 0x80000000u && s == -1) {
        c->z = 1; c->n = 0; c->v = 0; c->c = 0;
        return 0;
    }
    int32_t q = (int32_t)dst / s, r = (int32_t)dst % s;
    if (q == (int16_t)q) {
        c->z = q == 0; c->n = (q >> 15) & 1; c->v = 0; c->c = 0;
        return ((uint32_t)q & 0xffff) | ((uint32_t)r << 16);
    }
    c->v = 1;
    return dst;
}

/* ---- BCD (comportamiento de banderas indefinidas copiado de Musashi) ------------------ */
static inline uint32_t op_abcd(M68K *c, uint32_t src, uint32_t dst) {
    uint32_t res = (src & 0xf) + (dst & 0xf) + c->x;
    uint32_t vtmp = ~res;
    if (res > 9) res += 6;
    res += (src & 0xf0) + (dst & 0xf0);
    c->x = c->c = res > 0x99;
    if (c->c) res -= 0xa0;
    vtmp &= res;
    c->v = (vtmp >> 7) & 1;
    c->n = (res >> 7) & 1;
    res &= 0xff;
    if (res) c->z = 0;
    return res;
}
static inline uint32_t op_sbcd(M68K *c, uint32_t src, uint32_t dst) {
    uint32_t res = (dst & 0xf) - (src & 0xf) - c->x;
    uint32_t vtmp = ~res;
    if (res > 9) res -= 6;
    res += (dst & 0xf0) - (src & 0xf0);
    c->x = c->c = res > 0x99;
    if (c->c) res += 0xa0;
    res &= 0xff;
    vtmp &= res;
    c->v = (vtmp >> 7) & 1;
    c->n = (res >> 7) & 1;
    if (res) c->z = 0;
    return res;
}
static inline uint32_t op_nbcd(M68K *c, uint32_t dst) {
    dst &= 0xff;
    uint32_t res = (0x9a - dst - c->x) & 0xff;
    if (res != 0x9a) {
        uint32_t vtmp = ~res;
        if ((res & 0x0f) == 0xa) res = (res & 0xf0) + 0x10;
        res &= 0xff;
        vtmp &= res;
        c->v = (vtmp >> 7) & 1;
        if (res) c->z = 0;
        c->c = 1; c->x = 1;
    } else {
        c->v = 0; c->c = 0; c->x = 0;
    }
    c->n = (res >> 7) & 1;
    return res;
}

/* ---- desplazamientos y rotaciones (count ya enmascarado a 0..63) ---------------------- */
static inline uint32_t shmask(int bits) { return bits == 32 ? 0xffffffffu : ((1u << bits) - 1); }

static inline uint32_t op_asr(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    uint32_t msb = (val >> (bits - 1)) & 1;
    if (count == 0) { c->c = 0; c->n = msb; c->z = val == 0; c->v = 0; return val; }
    if (count < (uint32_t)bits) {
        uint32_t res = val >> count;
        if (msb) res |= mask & ~(mask >> count);
        c->c = c->x = (val >> (count - 1)) & 1;
        c->n = (res >> (bits - 1)) & 1; c->z = res == 0; c->v = 0;
        return res;
    }
    if (msb) { c->c = c->x = 1; c->n = 1; c->z = 0; c->v = 0; return mask; }
    c->c = c->x = 0; c->n = 0; c->z = 1; c->v = 0; return 0;
}
static inline uint32_t op_asl(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = 0; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    if (count < (uint32_t)bits) {
        uint32_t res = (val << count) & mask;
        c->x = c->c = (val >> (bits - count)) & 1;
        c->n = (res >> (bits - 1)) & 1; c->z = res == 0;
        uint32_t top = mask & ~(mask >> (count + 1));   /* los count+1 bits altos */
        uint32_t src = val & top;
        c->v = !(src == 0 || src == top);
        return res;
    }
    c->x = c->c = (count == (uint32_t)bits) ? (val & 1) : 0;
    c->n = 0; c->z = 1; c->v = val != 0;
    return 0;
}
static inline uint32_t op_lsr(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = 0; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    if (count <= (uint32_t)bits) {
        uint32_t res = count == 32 ? 0 : (val >> count);
        c->x = c->c = (val >> (count - 1)) & 1;
        c->n = 0; c->z = res == 0; c->v = 0;
        return res;
    }
    c->x = c->c = 0; c->n = 0; c->z = 1; c->v = 0;
    return 0;
}
static inline uint32_t op_lsl(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = 0; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    if (count < (uint32_t)bits) {
        uint32_t res = (val << count) & mask;
        c->x = c->c = (val >> (bits - count)) & 1;
        c->n = (res >> (bits - 1)) & 1; c->z = res == 0; c->v = 0;
        return res;
    }
    c->x = c->c = (count == (uint32_t)bits) ? (val & 1) : 0;
    c->n = 0; c->z = 1; c->v = 0;
    return 0;
}
static inline uint32_t rotl(int bits, uint32_t v, uint32_t s) {
    uint32_t mask = shmask(bits); v &= mask; s %= bits;
    if (s == 0) return v;
    return ((v << s) | (v >> (bits - s))) & mask;
}
static inline uint32_t rotr(int bits, uint32_t v, uint32_t s) {
    uint32_t mask = shmask(bits); v &= mask; s %= bits;
    if (s == 0) return v;
    return ((v >> s) | (v << (bits - s))) & mask;
}
static inline uint32_t op_ror(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = 0; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    uint32_t sh = count & (bits - 1);
    uint32_t res = rotr(bits, val, sh);
    c->c = (val >> ((sh - 1) & (bits - 1))) & 1;
    c->n = (res >> (bits - 1)) & 1; c->z = res == 0; c->v = 0;
    return res;
}
static inline uint32_t op_rol(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = 0; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    uint32_t sh = count & (bits - 1);
    uint32_t res = rotl(bits, val, sh);
    c->c = sh ? ((val >> (bits - sh)) & 1) : (val & 1);
    c->n = (res >> (bits - 1)) & 1; c->z = res == 0; c->v = 0;
    return res;
}
static inline uint32_t op_roxr(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = c->x; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    uint32_t sh = count % (bits + 1);
    uint64_t w = (uint64_t)val | ((uint64_t)c->x << bits);   /* bits+1 bits */
    uint64_t res = sh ? ((w >> sh) | (w << (bits + 1 - sh))) : w;
    res &= ((uint64_t)1 << (bits + 1)) - 1;
    c->c = c->x = (res >> bits) & 1;
    uint32_t r = (uint32_t)res & mask;
    c->n = (r >> (bits - 1)) & 1; c->z = r == 0; c->v = 0;
    return r;
}
static inline uint32_t op_roxl(M68K *c, int bits, uint32_t val, uint32_t count) {
    uint32_t mask = shmask(bits); val &= mask;
    if (count == 0) { c->c = c->x; c->n = (val >> (bits - 1)) & 1; c->z = val == 0; c->v = 0; return val; }
    uint32_t sh = count % (bits + 1);
    uint64_t w = (uint64_t)val | ((uint64_t)c->x << bits);
    uint64_t res = sh ? ((w << sh) | (w >> (bits + 1 - sh))) : w;
    res &= ((uint64_t)1 << (bits + 1)) - 1;
    c->c = c->x = (res >> bits) & 1;
    uint32_t r = (uint32_t)res & mask;
    c->n = (r >> (bits - 1)) & 1; c->z = r == 0; c->v = 0;
    return r;
}

/* ---- CHK ------------------------------------------------------------------------------ */
/* devuelve 1 si hay que lanzar la excepción CHK (vector 6) */
static inline int op_chk(M68K *c, uint32_t dn, uint32_t bound) {
    int32_t src = (int16_t)dn, b = (int16_t)bound;
    c->z = (dn & 0xffff) == 0; c->v = 0; c->c = 0;
    if (src >= 0 && src <= b) return 0;
    c->n = src < 0;
    return 1;
}

#endif /* RECOMP_CPU_H */
