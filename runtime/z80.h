/*
 * z80.h - intérprete Z80 para la CPU de sonido de la CPS1 (hito 4).
 *
 * Interpreta una instrucción por llamada (z80_step) y lleva el reloj en ciclos T absolutos.  Cada acceso
 * al bus recibe el ciclo exacto en que ocurre dentro de la instrucción: el comienzo de su ciclo M, con la
 * misma división en ciclos M que el Z80 real (M1 de 4 ciclos con la lectura del opcode al principio,
 * lecturas y escrituras de memoria de 3, E/S de 4 y los ciclos internos entre medias).  Ese instante es el
 * que importa a la placa de sonido: la lectura del latch del 68000 y del estado del YM2151 (ocupado,
 * temporizadores) dependen de él, y es también el que MAME ve en sus taps.
 *
 * Interrupciones: línea INT por nivel (z80_set_irq).  Se mira en cada límite de instrucción con IFF1 = 1,
 * salvo justo después de EI; la aceptación en IM 1 dura 13 ciclos (7 + dos escrituras en la pila) y a
 * continuación se ejecuta el primer opcode de 0x38.  En HALT la CPU repite ciclos M1 de 4 ciclos.
 * IM 0 e IM 2 se interpretan con el byte de bus 0xFF (RST 38h), que es lo que ve la CPS1 (sin daisy chain).
 *
 * Banderas: las documentadas y las no documentadas (X/Y, también tras SCF/CCF con el registro Q y tras
 * BIT con MEMPTR), como el núcleo de MAME.  El registro R cuenta los M1.
 */
#ifndef RUNTIME_Z80_H
#define RUNTIME_Z80_H

#include <stdint.h>
#include <stddef.h>
#include "sf2_port.h"

typedef struct Z80Bus {
    void *ctx;
    /* lectura de opcode (M1), lectura y escritura de datos, E/S; t = ciclo absoluto del acceso */
    uint8_t (*fetch)(void *ctx, uint16_t addr, uint64_t t);
    uint8_t (*read)(void *ctx, uint16_t addr, uint64_t t);
    void (*write)(void *ctx, uint16_t addr, uint8_t val, uint64_t t);
    uint8_t (*in)(void *ctx, uint16_t port, uint64_t t);
    void (*out)(void *ctx, uint16_t port, uint8_t val, uint64_t t);
} Z80Bus;

/* par de registros; acceso por bytes con .b.h / .b.l, con el orden de los bytes del anfitrión (sf2_port.h) */
typedef union Z80Pair {
    uint16_t w;
#if SF2_LITTLE_ENDIAN
    struct { uint8_t l, h; } b;
#else
    struct { uint8_t h, l; } b;
#endif
} Z80Pair;

typedef struct Z80 {
    Z80Pair af, bc, de, hl, ix, iy, wz;
    uint16_t sp, pc;
    Z80Pair af2, bc2, de2, hl2;
    uint8_t i, r, r2;            /* r: 7 bits que cuentan los M1; r2: bit 7 fijado por LD R,A */
    uint8_t iff1, iff2, im, halt;
    uint8_t after_ei;            /* la instrucción anterior fue EI: no se acepta INT en este límite */
    uint8_t irq_line;            /* nivel de la línea INT */
    uint8_t q, qset;             /* registro Q de MAME: 0x28 si la instrucción anterior no tocó F, si no 0
                                  * (SCF/CCF toman X/Y de (F & q) | A); qset lo marca la instrucción en curso */
    uint64_t cycles;             /* ciclos T desde el reset */
    uint64_t insns, irqs;        /* instrucciones ejecutadas e interrupciones aceptadas */
    Z80Bus bus;
} Z80;

struct StateStream;
void z80_state(Z80 *z, struct StateStream *s);      /* registros y contadores (el bus no se guarda) */
void z80_reset(Z80 *z);
void z80_set_irq(Z80 *z, int level);
/* ejecuta una instrucción, una aceptación de interrupción o un ciclo de HALT; devuelve los ciclos T */
int z80_step(Z80 *z);

/* desensamblado de la instrucción en pc (lee la memoria con peek); devuelve su longitud en bytes */
int z80_disasm(uint16_t pc, uint8_t (*peek)(void *ctx, uint16_t addr), void *ctx, char *buf, size_t n);

#endif
