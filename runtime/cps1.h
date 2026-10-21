/*
 * cps1.h - máquina CPS1 mínima sobre el código recompilado (hito 2): reloj de cuadro, entradas por
 * cuadro y resumen por cuadro comparable con el que produce tools/mame_blind.lua en MAME.
 *
 * Reloj: el 68000 va a 10 MHz y la pantalla CPS1 a 8 MHz de píxel con 512 x 262 ciclos por cuadro
 * (cps1.h de MAME), es decir 59,637 Hz: 167.680 ciclos de CPU por cuadro, no 166.666.
 *
 * Cuadro: se cuenta cada entrega de la IRQ2 (VBlank).  Durante el test de RAM del arranque la IRQ2
 * está enmascarada y los VBlank solapados se funden en una sola entrega, igual que en MAME (la línea
 * IPL1 queda activa hasta el acuse), así que la cuenta coincide en ambos lados.
 *
 * Punto de muestra: la instrucción 0xAD0 del manejador de IRQ2 (`move.w $800172.l,0x5e(a5)`), la
 * primera lectura de E/S del manejador.  En MAME un tap de lectura sobre 0x800172 con CURPC = 0xAD0
 * dispara en el mismo instante.  En ese punto d0-d7 y a0-a4/a6 son los del código interrumpido,
 * a5 = 0xFFFF8000, a7 = SSP tras el movem, y el SR y PC interrumpidos están en (a7+60) y (a7+62).
 *
 * Resumen por cuadro (una línea, campos separados por espacios, hexadecimal sin prefijo):
 *   f=N pc=PPPPPP sr=SSSS d=d0,...,d7 a=a0,...,a7 in=II,IIII,II fc=FF w=HHHHHHHH v=HHHHHHHH g=HHHHHHHH
 *   [wb=16 hashes gb=12 hashes]
 *   w: FNV-1a (palabras de 16 bits big-endian) de la RAM de trabajo sin las zonas volátiles
 *   v: FNV-1a de las zonas volátiles: bloques de tarea, variables del planificador y pilas de usuario
 *      (0xFF0000-0xFF0A4F) y pila supervisora (0xFFFF00-0xFFFFFF), que dependen del punto exacto en
 *      que la IRQ interrumpe el bucle de espera
 *   g: FNV-1a de la RAM gráfica completa
 *   fc: contador de cuadros del propio juego (byte 0xFFFF801C)
 */
#ifndef RECOMP_CPS1_H
#define RECOMP_CPS1_H

#include <stdint.h>
#include <stdio.h>
#include "mem.h"

struct M68K;

#define CPS1_CPU_HZ        10000000u
#define CPS1_FRAME_CYCLES  167680u       /* 10 MHz * 512 * 262 / 8 MHz */
#define CPS1_IRQ2_HANDLER  0x000AB8u
#define CPS1_SAMPLE_PC     0x000AD0u
#define CPS1_FRAME_COUNTER 0xFF801Cu     /* byte 0x1C(a5) incrementado por el manejador de IRQ2 en 0xAF4 (a5 = 0xFFFF8000) */
#define CPS1_VBLANK_FLAG   0xFF020Eu     /* byte puesto a 0xFF por la IRQ2 (0xAF8) y borrado por el planificador (0xA30) */

/* zonas volátiles de la RAM de trabajo (ver arriba) */
#define CPS1_VOLATILE1_LO  0xFF0000u
#define CPS1_VOLATILE1_HI  0xFF0A50u
#define CPS1_VOLATILE2_LO  0xFFFF00u
#define CPS1_VOLATILE2_HI  0x1000000u

#define CPS1_WRAM_BLOCKS 16     /* bloques de 4 KiB para el modo detalle */
#define CPS1_GFX_BLOCKS  12     /* bloques de 16 KiB */

typedef struct InputEvent {
    uint64_t from, to;      /* cuadros, inclusive */
    uint8_t port;           /* 0 = IN0, 1 = IN1, 2 = IN2 */
    uint16_t mask;
} InputEvent;

typedef struct Cps1 {
    Mem *mem;
    uint64_t frame;             /* entregas de IRQ2 hasta ahora */
    uint64_t frame_cycles;      /* ciclos de CPU por cuadro */
    InputEvent *events;
    int nevents;
    uint16_t manual_in0, manual_in1, manual_in2;   /* bits pulsados por el anfitrión (visor con --keyboard), se suman a la tabla */
    /* herramientas de desarrollo (sf2_set_dev_tool): bits de in1 que se pulsan hasta el cuadro `until` inclusive; fuera del estado */
    uint16_t dev_in1[2];
    uint64_t dev_until[2];
    FILE *summary;              /* resumen por cuadro (validación); lo abre y cierra el anfitrión */
    int detail;
    uint64_t samples;           /* líneas de resumen escritas */
} Cps1;

void cps1_init(Cps1 *m, Mem *mem);
/* interpreta la secuencia de entradas (texto, formato: "desde hasta tecla", ver tools/inputs_sf2.txt);
 * devuelve el número de eventos, -1 sin memoria o -2 si hay líneas mal formadas (que se describen en `err`,
 * una por línea, con `name` como nombre de archivo).  El núcleo no lee archivos: los lee el anfitrión. */
int cps1_parse_inputs(Cps1 *m, const char *text, const char *name, char *err, size_t errn);
/* fija in0/in1/in2 del Mem para el cuadro dado y latchea los valores leíbles */
void cps1_apply_inputs(Cps1 *m, uint64_t frame);
/* llamar al entregar la IRQ2 (antes de apilar el marco): cuenta el cuadro y aplica sus entradas */
void cps1_on_irq2(Cps1 *m, struct M68K *c);
/* llamar al llegar a CPS1_SAMPLE_PC: escribe la línea de resumen y, si toca, vuelca la RAM */
void cps1_sample(Cps1 *m, struct M68K *c);

struct StateStream;
void cps1_state(Cps1 *m, struct StateStream *s);       /* contador de cuadros (lo demás es configuración del anfitrión) */

uint32_t cps1_hash_wram(const Mem *mem);
uint32_t cps1_hash_wram_volatile(const Mem *mem);
uint32_t cps1_hash_gfx(const Mem *mem);
/* nombre de tecla -> (puerto, máscara); devuelve 0 si no existe */
int cps1_input_lookup(const char *name, uint8_t *port, uint16_t *mask);

#endif
