/*
 * gfx.h - ROMs gráficas de SF2 (World 910522) y mapeador de bancos STF29 (hito 3).
 *
 * Región "gfx" de MAME (ROM_START( sf2 ), ROM_LOAD64_WORD): 6 MiB formados por tres grupos de cuatro
 * ROMs de 512 KiB.  Cada ROM aporta una palabra de 16 bits (dos bytes en el orden del archivo) cada 8
 * bytes de la región: la ROM cargada en el offset 0 ocupa los bytes 0-1, 8-9, 16-17...; la del offset 2
 * los bytes 2-3, 10-11...; etc.  Nombres de nuestro set (sf2/) y posición:
 *
 *   grupo 0 (banco 0, 0x000000): sf2_06.bin +0, sf2_08.bin +2, sf2_05.bin +4, sf2_07.bin +6
 *   grupo 1 (banco 1, 0x200000): sf2_15.bin +0, sf2_17.bin +2, sf2_14.bin +4, sf2_16.bin +6
 *   grupo 2 (banco 2, 0x400000): sf2_25.bin +0, sf2_27.bin +2, sf2_24.bin +4, sf2_26.bin +6
 *
 * Formato de píxeles (gfx_layout de cps1.cpp): 4 bits por píxel en 4 planos.  La unidad es un grupo de
 * 8 bytes = 16 píxeles de una fila: los bytes 0-3 son los 8 píxeles de la izquierda (byte 0 = plano del
 * bit 0 del pen, byte 1 = bit 1, byte 2 = bit 2, byte 3 = bit 3; el bit 7 de cada byte es el píxel de más
 * a la izquierda) y los bytes 4-7 los 8 píxeles de la derecha.  Sobre esa unidad:
 *
 *   8x8 (SCROLL1):  64 bytes por tile, una unidad por fila; las columnas pares del tilemap usan la mitad
 *                   izquierda de la unidad y las impares la derecha (cps1_layout8x8 y cps1_layout8x8_2,
 *                   gfxset = bit 5 del índice de tile).
 *   16x16 (OBJ y SCROLL2): 128 bytes por tile, una unidad (16 píxeles) por fila.
 *   32x32 (SCROLL3): 512 bytes por tile, dos unidades consecutivas por fila.
 *
 * gfx_init() intercala las doce ROMs y predecodifica toda la región en "pens": un byte (0-15) por píxel,
 * 16 por unidad, de modo que el renderizador solo indexa.
 *
 * Mapeador STF29 (PAL del B-board 90629B-3, tabla mapper_STF29_table de cps1_v.cpp): el código de tile
 * que escribe el juego se convierte a unidades de 8x8 (desplazamiento 1 para sprites y SCROLL2, 0 para
 * SCROLL1, 3 para SCROLL3), se busca en los rangos permitidos y se traduce al índice de tile dentro de
 * la región; fuera de rango el tile es transparente (gfx_map devuelve -1).  Para SF2 los rangos son:
 *
 *   OBJ     0x0000-0x3FFF banco 0, 0x4000-0x7FFF banco 1, 0x8000-0x8FFF banco 2   (identidad)
 *   SCROLL3 0x0400-0x07FF banco 2  -> tile 0x2000 + código (32x32: bytes 0x400000 + ...)
 *   SCROLL1 0x4000-0x4FFF banco 2  -> tile 0x10000 + código (8x8: bytes 0x400000 + ...)
 *   SCROLL2 0x2800-0x3FFF banco 2  -> tile 0x8000 + código (16x16: bytes 0x400000 + ...)
 */
#ifndef RECOMP_GFX_H
#define RECOMP_GFX_H

#include <stdint.h>
#include <stddef.h>
#include "romset.h"

#define GFX_ROM_SIZE   0x600000u
#define GFX_UNITS      (GFX_ROM_SIZE / 8)      /* unidades de 16 píxeles */

#define GFXTYPE_SPRITES 1
#define GFXTYPE_SCROLL1 2
#define GFXTYPE_SCROLL2 4
#define GFXTYPE_SCROLL3 8

typedef struct Gfx {
    uint8_t *rom;      /* 6 MiB, región "gfx" de MAME: solo existe mientras gfx_init decodifica (después NULL) */
    uint8_t *pens;     /* GFX_UNITS * 16 pens predecodificados */
    /* hito 6 (rendimiento): gfx_map precalculado para cada código de 16 bits de los cuatro tipos (sprites, SCROLL1-3)
     * y la conversión de una palabra de paleta a 0x00RRGGBB con el brillo, para no repetir divisiones por cuadro */
    int32_t *map[4];
    uint32_t *pal_rgb;
    /* pantalla ancha (D54): para cada código de 16 bits de SCROLL2 (0) y SCROLL3 (1), 1 si su tile es transparente (fuera del
     * mapeador o con los 16/32x(16/32) pens a 15); SCROLL3 se indexa con el código ya enmascarado a 0x3fff */
    uint8_t *blank[2];
} Gfx;

/* intercala las doce ROMs gráficas del RomSet (nombres y posiciones en romset.c) y predecodifica; 0 o -1 */
int gfx_init(Gfx *g, const RomSet *rs);
void gfx_free(Gfx *g);

/* mapeador STF29: índice de tile del tipo dado (en unidades de su tamaño) o -1 si está fuera de rango */
int gfx_map(int type, int code);
/* lo mismo con las tablas de gfx_init (type: GFXTYPE_*; code de 16 bits) */
static inline int gfx_map_fast(const Gfx *g, int type, int code) {
    int t = type == 1 ? 0 : type == 2 ? 1 : type == 4 ? 2 : 3;
    return g->map[t][code & 0xffff];
}
/* palabra de paleta del CPS-B (brillo en los bits 12-15) a 0x00RRGGBB, como palette_w/cps1_build_palette de MAME */
uint32_t gfx_palette_rgb(uint16_t p);

/* pens de una fila de tile: puntero a 8 (8x8), 16 (16x16) o 32 (32x32) bytes consecutivos */
static inline const uint8_t *gfx_row8(const Gfx *g, int tile, int y, int gfxset) {
    return g->pens + ((size_t)tile * 8 + (size_t)y) * 16 + (gfxset ? 8 : 0);
}
static inline const uint8_t *gfx_row16(const Gfx *g, int tile, int y) {
    return g->pens + ((size_t)tile * 16 + (size_t)y) * 16;
}
static inline const uint8_t *gfx_row32(const Gfx *g, int tile, int y) {
    return g->pens + ((size_t)tile * 64 + (size_t)y * 2) * 16;   /* 512 bytes = 64 unidades por tile, 2 por fila */
}

#endif
