#include "gfx.h"
#include <stdlib.h>
#include <string.h>

#define GFX_ROM_CHIP 0x80000u

/* pens de una unidad de 8 bytes (16 píxeles); orden de planos {24,16,8,0} de cps1_layout*: el plano
 * del byte 3 es el bit 3 del pen y el bit 7 de cada byte es el píxel de más a la izquierda */
static void decode_unit(const uint8_t *b, uint8_t *out) {
    for (int x = 0; x < 8; x++) {
        int sh = 7 - x;
        out[x] = (uint8_t)((((b[3] >> sh) & 1) << 3) | (((b[2] >> sh) & 1) << 2) | (((b[1] >> sh) & 1) << 1) | ((b[0] >> sh) & 1));
        out[8 + x] = (uint8_t)((((b[7] >> sh) & 1) << 3) | (((b[6] >> sh) & 1) << 2) | (((b[5] >> sh) & 1) << 1) | ((b[4] >> sh) & 1));
    }
}

int gfx_init(Gfx *g, const RomSet *rs) {
    const GameDef *game = rs->game;
    memset(g, 0, sizeof *g);
    if (game->gfx_size > GFX_ROM_SIZE) return -1;
    g->mapper = game->mapper;
    g->rom = calloc(GFX_ROM_SIZE, 1);
    g->pens = malloc((size_t)GFX_UNITS * 16);
    if (!g->rom || !g->pens) { gfx_free(g); return -1; }
    /* ROM_LOAD64_WORD del ROM_START: la palabra w de cada ROM va al offset `dest + 8 * w` de la región */
    for (int i = 0; i < game->nroms; i++) {
        const RomInfo *ri = &game->roms[i];
        if (ri->kind != ROMKIND_GFX)
            continue;
        if (!rs->data[i] || ri->dest + 4u * ri->size - 6u > GFX_ROM_SIZE) { gfx_free(g); return -1; }
        const uint8_t *chip = rs->data[i];
        uint8_t *dst = g->rom + ri->dest;
        for (uint32_t w = 0; w < GFX_ROM_CHIP / 2; w++) {
            dst[w * 8] = chip[w * 2];
            dst[w * 8 + 1] = chip[w * 2 + 1];
        }
    }
    for (uint32_t u = 0; u < GFX_UNITS; u++)
        decode_unit(g->rom + (size_t)u * 8, g->pens + (size_t)u * 16);
    free(g->rom);       /* el renderizador solo usa los pens: la región cruda (6 MiB) ya no hace falta */
    g->rom = NULL;
    static const int types[4] = { GFXTYPE_SPRITES, GFXTYPE_SCROLL1, GFXTYPE_SCROLL2, GFXTYPE_SCROLL3 };
    for (int t = 0; t < 4; t++) {
        g->map[t] = malloc(0x10000 * sizeof *g->map[t]);
        if (!g->map[t]) { gfx_free(g); return -1; }
        for (int code = 0; code < 0x10000; code++)
            g->map[t][code] = gfx_map(g->mapper, types[t], code);
    }
    for (int l = 0; l < 2; l++) {
        const int type = l == 0 ? GFXTYPE_SCROLL2 : GFXTYPE_SCROLL3, bytes = l == 0 ? 16 : 32, rows = bytes;
        g->blank[l] = malloc(0x10000);
        if (!g->blank[l]) { gfx_free(g); return -1; }
        for (int code = 0; code < 0x10000; code++) {
            int tile = gfx_map(g->mapper, type, code), blank = 1;
            for (int y = 0; tile >= 0 && blank && y < rows; y++) {
                const uint8_t *r = l == 0 ? gfx_row16(g, tile, y) : gfx_row32(g, tile, y);
                for (int x = 0; x < bytes; x++)
                    if (r[x] != 15) { blank = 0; break; }
            }
            g->blank[l][code] = (uint8_t)blank;
        }
    }
    g->pal_rgb = malloc(0x10000 * sizeof *g->pal_rgb);
    if (!g->pal_rgb) { gfx_free(g); return -1; }
    for (int p = 0; p < 0x10000; p++)
        g->pal_rgb[p] = gfx_palette_rgb((uint16_t)p);
    return 0;
}

void gfx_free(Gfx *g) {
    free(g->rom);
    free(g->pens);
    for (int t = 0; t < 4; t++) {
        free(g->map[t]);
        g->map[t] = NULL;
    }
    free(g->pal_rgb);
    g->pal_rgb = NULL;
    for (int l = 0; l < 2; l++) {
        free(g->blank[l]);
        g->blank[l] = NULL;
    }
    g->rom = g->pens = NULL;
}

uint32_t gfx_palette_rgb(uint16_t p) {
    int bright = 0x0f + ((p >> 12) << 1);      /* brillo 0 = un tercio */
    int r = ((p >> 8) & 0x0f) * 0x11 * bright / 0x2d;
    int gg = ((p >> 4) & 0x0f) * 0x11 * bright / 0x2d;
    int b = ((p >> 0) & 0x0f) * 0x11 * bright / 0x2d;
    return ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
}

/* gfx_range de cps1_v.cpp (mapper_STF29, mapper_S9263B... en game.c): el código se lleva a unidades de 8x8, se busca su
 * rango y se traduce a la posición dentro de la región */
int gfx_map(const GfxMapper *mp, int type, int code) {
    int shift;
    switch (type) {
    case GFXTYPE_SPRITES: shift = 1; break;
    case GFXTYPE_SCROLL1: shift = 0; break;
    case GFXTYPE_SCROLL2: shift = 1; break;
    case GFXTYPE_SCROLL3: shift = 3; break;
    default: return -1;
    }
    code <<= shift;
    for (int i = 0; i < mp->nranges; i++) {
        const GfxRange *r = &mp->ranges[i];
        if (code >= r->start && code <= r->end && (r->type & type)) {
            int base = 0;
            for (int b = 0; b < r->bank; b++) base += mp->bank_sizes[b];
            return (base + (code & (mp->bank_sizes[r->bank] - 1))) >> shift;
        }
    }
    return -1;
}
