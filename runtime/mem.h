/*
 * mem.h - mapa de memoria CPS1 (Street Fighter II, placa CPS-B-11) visto por el 68000.
 *
 *   0x000000-0x0FFFFF  ROM de programa (1 MiB, cuatro pares de chips)
 *   0x100000-0x3FFFFF  zona de ROM sin poblar: lee 0x0000 (igual que MAME)
 *   0x800000-0x8001FF  E/S: entradas, dipswitches, control de monedas, CPS-A, CPS-B, latches de sonido
 *   0x900000-0x92FFFF  RAM gráfica (192 KiB)
 *   0xFF0000-0xFFFFFF  RAM de trabajo (64 KiB)
 *   resto              sin mapear: lee 0x0000, la escritura se descarta (contrastado con MAME 0.289)
 *
 * Zona de E/S (fuente: cps1.cpp y cps1_v.cpp de MAME, configuración {"sf2", CPS_B_11, mapper_STF29, 0x36}):
 *
 *   0x800000-0x800007 R  IN1: joysticks y botones 1-3 (bits 0-7 jugador 1, 8-15 jugador 2), activo a nivel bajo
 *   0x800018          R  IN0 << 8 | 0xFF: monedas (bits 0-1), servicio (2), start 1/2 (4-5), modo test (6)
 *   0x80001A/1C/1E    R  DSWA/DSWB/DSWC << 8 | 0xFF
 *   0x800020          R  0 (nopr en MAME)
 *   0x800030-0x800037 W  control de monedas (solo el byte alto): bits 8-9 contadores, 10-11 bloqueo (1 = sin bloqueo)
 *   0x800100-0x80013F W  CPS-A (solo escritura; la lectura devuelve 0 como una zona sin mapear)
 *   0x800140-0x80017F RW CPS-B: 0x800172 lee 0x0401 (ID del CPS-B-11), 0x800176 lee IN2 (botones 4-6, byte bajo),
 *                        0x800140 lee (reg0 * reg0) & 0xFFFF (resto del multiplicador de MAME), los demás leen 0xFFFF
 *   0x800180-0x800187 W  latch de sonido (byte bajo; si solo se escribe el byte alto, el byte alto)
 *   0x800188-0x80018F W  latch 2 de sonido / fade (byte bajo)
 *
 * Cada lado del arnés (recompilado / Musashi) tiene su propia instancia Mem con RAM propia; la ROM se
 * comparte.  La E/S registra cada acceso por dirección (tabla imprimible con mem_print_io_table) y las
 * escrituras de 16 bits de la instrucción en curso (wlog) para que el arnés compare lados.
 */
#ifndef RECOMP_MEM_H
#define RECOMP_MEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define ROM_SIZE     0x100000u
#define GFXRAM_BASE  0x900000u
#define GFXRAM_SIZE  0x30000u
#define WRAM_BASE    0xFF0000u
#define WRAM_SIZE    0x10000u
#define IO_BASE      0x800000u
#define IO_SIZE      0x200u
#define IO_WORDS     (IO_SIZE / 2)

/* offsets dentro de la zona de E/S */
#define IO_IN1        0x000
#define IO_IN0_DSW    0x018
#define IO_COINCTRL   0x030
#define IO_CPSA       0x100
#define IO_CPSB       0x140
#define IO_SOUND      0x180
#define IO_SOUND2     0x188
#define CPSB_ID_OFFSET   0x32      /* 0x800172 */
#define CPSB_ID_VALUE    0x0401
#define CPSB_IN2_OFFSET  0x36      /* 0x800176 */

/* registros CPS-A (índice de palabra desde 0x800100) y CPS-B-11 (desde 0x800140), como cps1.h de MAME */
#define CPSA_OBJ_BASE      (0x00 / 2)
#define CPSA_SCROLL1_BASE  (0x02 / 2)
#define CPSA_SCROLL2_BASE  (0x04 / 2)
#define CPSA_SCROLL3_BASE  (0x06 / 2)
#define CPSA_OTHER_BASE    (0x08 / 2)
#define CPSA_PALETTE_BASE  (0x0a / 2)
#define CPSA_SCROLL1_X     (0x0c / 2)
#define CPSA_SCROLL1_Y     (0x0e / 2)
#define CPSA_SCROLL2_X     (0x10 / 2)
#define CPSA_SCROLL2_Y     (0x12 / 2)
#define CPSA_SCROLL3_X     (0x14 / 2)
#define CPSA_SCROLL3_Y     (0x16 / 2)
#define CPSA_ROWSCROLL_OFFS (0x20 / 2)
#define CPSA_VIDEOCONTROL  (0x22 / 2)
#define CPSB11_LAYER_CONTROL (0x26 / 2)     /* 0x800166 */
#define CPSB11_PRIORITY0   (0x28 / 2)       /* 0x800168-0x80016E */
#define CPSB11_PALETTE_CONTROL (0x30 / 2)   /* 0x800170 */
#define CPSB11_LAYER1_ENABLE 0x08           /* bits de habilitación de SCROLL1/2/3 en el control de capas */
#define CPSB11_LAYER2_ENABLE 0x10
#define CPSB11_LAYER3_ENABLE 0x20

#define PALETTE_PAGES   6
#define PALETTE_ENTRIES (PALETTE_PAGES * 0x200)   /* 0xC00 pens: OBJ, SCROLL1, SCROLL2, SCROLL3, estrellas 1 y 2 */

/* Valores por defecto de los dipswitches (INPUT_PORTS_START( sf2 ) de MAME, puertos leídos en el arranque):
 *   DSWA 0xFF: 1 moneda 1 crédito, sin "2 monedas para empezar"
 *   DSWB 0xFC: dificultad 3 ("Normal", bits 0-2 = 100), resto sin uso
 *   DSWC 0x9F: sonido en demo (bit 5 = 0), continuar permitido (bit 6 = 0), modo juego (bit 7 = 1) */
#define MEM_DSWA_DEFAULT 0xff
#define MEM_DSWB_DEFAULT 0xfc
#define MEM_DSWC_DEFAULT 0x9f

#define WLOG_MAX 64

typedef struct WriteLog {
    uint32_t addr;
    uint16_t val;
} WriteLog;

typedef struct IoStat {
    uint64_t reads, writes;
    uint16_t last_read, last_write;
} IoStat;

typedef struct Mem {
    const uint8_t *rom;
    uint8_t *gfxram;
    uint8_t *wram;
    uint16_t cps_a[0x20];
    uint16_t cps_b[0x20];
    /* RAM de paleta propia del CPS-B (hito 3): el juego escribe la paleta en la RAM gráfica y el chip la
     * copia aquí cuando la CPU escribe la base de paleta (0x80010A), página a página según el control de
     * paleta (0x800170), igual que cps1_build_palette en MAME.  Valores crudos xBBBB RRRR GGGG BBBB
     * (brillo en los bits 12-15); la conversión a RGB la hace el renderizador. */
    uint16_t palette[PALETTE_ENTRIES];
    uint64_t palette_copies;
    uint16_t soundlatch, soundlatch2;
    uint16_t coinctrl;          /* última palabra escrita en 0x800030 (solo cuenta el byte alto) */
    uint64_t coin_counter[2];   /* pulsos de los contadores de monedas (flancos de subida de los bits 8 y 9) */
    uint64_t sound_writes;      /* comandos enviados al Z80 (para el informe) */
    /* escritura en un latch de sonido (which = 1 para 0x800181, 2 para 0x800189): la placa de sonido la
     * recibe con el ciclo del 68000, que conoce quien instala el gancho (runtime/machine.c) */
    void (*sound_latch_cb)(void *ctx, int which, uint8_t val);
    void *sound_ctx;
    /* entradas "pulsadas" (activas a nivel bajo), tal como las pondría el jugador */
    uint16_t in0, in1, in2, dswa, dswb, dswc;
    /* valor que leen los puertos este cuadro: in0 con el bloqueo de monedas aplicado (se fija en cada IRQ2,
     * como la actualización por cuadro de los puertos de MAME) */
    uint16_t in0_live, in1_live, in2_live;
    const char *name;        /* "recomp" / "musashi" para los mensajes */
    int log_io;              /* registrar accesos de E/S y sin mapear en log_file (depuración) */
    FILE *log_file;
    uint64_t io_reads, io_writes, unmapped;
    IoStat io[IO_WORDS];     /* accesos por palabra de la zona de E/S */
    /* log de escrituras de 16 bits de la instrucción en curso (para comparar lados) */
    int nlog;
    WriteLog wlog[WLOG_MAX];
} Mem;

void mem_init(Mem *m, const uint8_t *rom, const char *name);
void mem_free(Mem *m);
/* apaga y enciende: RAM, registros, paleta y latches a cero; conserva ROM, dipswitches y ganchos */
void mem_reset(Mem *m);
struct StateStream;
/* estado guardable: RAM gráfica y de trabajo, registros, paleta, latches, bloqueo de monedas y entradas latcheadas */
void mem_state(Mem *m, struct StateStream *s);
void mem_reset_log(Mem *m);
/* fija in*_live a partir de in0/in1/in2 y del bloqueo de monedas vigente (llamar en cada IRQ2) */
void mem_latch_inputs(Mem *m);
/* copia entradas y valores latcheados de otra instancia (ambos lados del arnés ven lo mismo) */
void mem_copy_inputs(Mem *dst, const Mem *src);

uint32_t mem_read8(Mem *m, uint32_t addr);
uint32_t mem_read16(Mem *m, uint32_t addr);
uint32_t mem_read32(Mem *m, uint32_t addr);
void mem_write8(Mem *m, uint32_t addr, uint32_t val);
void mem_write16(Mem *m, uint32_t addr, uint32_t val);
void mem_write32(Mem *m, uint32_t addr, uint32_t val);

/* copia la paleta de la RAM gráfica a m->palette según la base y el control de paleta (se llama sola al
 * escribir 0x80010A; expuesta para pruebas y para reconstruir la paleta desde un volcado) */
void mem_palette_copy(Mem *m);

/* compara RAM de trabajo + RAM gráfica + registros; devuelve la primera dirección distinta o -1 */
int64_t mem_compare(const Mem *a, const Mem *b);

/* tabla (markdown) de todas las direcciones de E/S tocadas, con conteos y descripción */
void mem_print_io_table(const Mem *m, FILE *f);
/* nombre descriptivo de una dirección de E/S (o "" si no se conoce) */
const char *mem_io_name(uint32_t addr);

#endif
