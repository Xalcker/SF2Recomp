#include "video.h"
#include "state.h"
#include "sf2_port.h"
#include <stdlib.h>
#include <string.h>

void video_init(Video *v, const Gfx *gfx) {
    memset(v, 0, sizeof *v);
    v->gfx = gfx;
    v->layer_mask = 0xf;
    v->w = v->rw = VIDEO_W;
    v->rx0 = VIDEO_RAW_X0;
}

int video_set_width(Video *v, int w) {
    if (w < VIDEO_W || w > VIDEO_W_MAX || (w & 1))
        return -1;
    v->w = w;
    return 0;
}

void video_set_hud_edges(Video *v, int on) { v->hud_edges = on != 0; }
void video_set_xsigned(Video *v, int on) { v->xsigned = on != 0; }

/* El marcador del combate son sprites en ranuras fijas de la tabla de objetos (D55): 0-13 puntuación del 1P y del 2P / INSERT
 * COIN, 14-25 tiempo y récord (centro), 26-36 nombre y marca de rondas del 1P, 37-43 nombre del 2P, 44-63 barras de vida y 72-75
 * marcadores de vida (código 0x8302) y el KO; 64-71 son los mensajes centrales (ROUND, recuento) y de la 76 en adelante empiezan los luchadores y
 * los efectos.  Devuelve -1 si el sprite va a la izquierda, 1 a la derecha y 0 si se queda donde está; `cx` es el centro del sprite
 * en coordenadas crudas: las dos mitades se separan por el centro de la placa (256); el KO, el tiempo y el récord quedan en medio. */
static int hud_side(int idx, int cx, int y, int code) {
    if (y >= 80)
        return 0;                                       /* el marcador está en la banda superior; el INSERT COIN del demo usa las mismas ranuras más abajo */
    if (idx >= 76 || (idx >= 64 && idx <= 71) || (idx >= 14 && idx <= 25))
        return 0;
    if (idx >= 72 && code != 0x8302)
        return 0;                                       /* el KO y el texto grande de la ronda; solo se mueven los marcadores */
    return cx < 256 ? -1 : 1;
}

int video_scene_wide(const Mem *m) {
    const uint8_t *ram = m->wram;
    const int mode = ram[0x8001], phase = ram[0x8005], sub = ram[0x8009];
    int fight;
    if (mode == 2 && phase == 4) {
        /* partida: subfase 0x08 presentación de la ronda, 0x0A pelea, 0x0C victoria, 0x10 recuento de puntos; el resto de
         * subfases son el VS, el mapa y el diálogo del vencido */
        fight = sub == 0x08 || sub == 0x0a || sub == 0x0c || sub == 0x10;
    } else {
        /* demostración (modo atracción): modo 0x0A con fase 2 es la selección (subfase 0) y el combate (subfase 2); modo
         * 0x0C con fase 2 es su victoria; el ranking y el título tienen otras fases */
        fight = (mode == 0x0a && phase == 2 && sub == 2) || (mode == 0x0c && phase == 2);
    }
    return fight;
}

/* palabra big-endian de la RAM gráfica; fuera de rango (bases altas que MAME leería fuera de su array) lee 0 */
static inline uint16_t gfx_word(const Mem *m, uint32_t off) {
    if (off + 1 >= GFXRAM_SIZE)
        return 0;
    return (uint16_t)((m->gfxram[off] << 8) | m->gfxram[off + 1]);
}

/* cps1_base de MAME: registro * 256 alineado al tamaño de la zona, dentro de 0x40000 */
static inline uint32_t gfx_base(const Mem *m, int reg, uint32_t boundary) {
    return ((uint32_t)m->cps_a[reg] * 256u) & ~(boundary - 1) & 0x3ffffu;
}

void video_latch_obj(Video *v, const Mem *m) {
    uint32_t base = gfx_base(m, CPSA_OBJ_BASE, 0x800);
    for (int i = 0; i < OBJ_WORDS; i++)
        v->obj[i] = gfx_word(m, base + 2u * (uint32_t)i);
    v->latches++;
}

/* ---- paleta ---------------------------------------------------------------------------- */
static void build_rgb(Video *v, const Mem *m) {
    const uint32_t *lut = v->gfx->pal_rgb;          /* gfx_palette_rgb de las 65.536 palabras, calculado en gfx_init */
    for (int i = 0; i < PALETTE_ENTRIES; i++)
        v->rgb[i] = lut[m->palette[i]];
}

/* ---- capas de scroll ------------------------------------------------------------------- */
/* índice de tile en la RAM de la capa para (columna, fila) de 0-63 (tilemapN_scan de MAME) */
static inline int scan_index(int layer, int col, int row) {
    switch (layer) {
    case 1: return (row & 0x1f) + ((col & 0x3f) << 5) + ((row & 0x20) << 6);
    case 2: return (row & 0x0f) + ((col & 0x3f) << 4) + ((row & 0x30) << 6);
    default: return (row & 0x07) + ((col & 0x3f) << 3) + ((row & 0x38) << 6);
    }
}

/* lo que el renderizador necesita de un tile del tilemap (se calcula una vez por fila de tiles y columna) */
typedef struct TileInfo {
    const uint8_t *pens;    /* fila 0 del tile (gfx_rowN con y = 0), NULL si el mapeador lo deja fuera (todo pen 15) */
    uint16_t cb, mask;      /* primer pen de su color; máscara de prioridad de su grupo */
    uint8_t flipx, flipy;
} TileInfo;

/* n píxeles de un tramo de tile: pens src[i] (o src[-i] volteado); con n constante el compilador lo vectoriza entero */
static SF2_ALWAYS_INLINE void span_draw(uint16_t *dst, const uint8_t *src, int n, int cb, int flipx) {
    if (flipx)
        for (int i = 0; i < n; i++) { const int pen = src[-i]; dst[i] = pen == 15 ? dst[i] : (uint16_t)(cb + pen); }
    else
        for (int i = 0; i < n; i++) { const int pen = src[i]; dst[i] = pen == 15 ? dst[i] : (uint16_t)(cb + pen); }
}

static SF2_ALWAYS_INLINE int span_mark(uint8_t *dp, const uint8_t *src, int n, unsigned mask, int flipx) {
    int marked = 0;
    if (flipx)
        for (int i = 0; i < n; i++) { const int bit = (mask >> src[-i]) & 1; dp[i] |= (uint8_t)bit; marked += bit; }
    else
        for (int i = 0; i < n; i++) { const int bit = (mask >> src[i]) & 1; dp[i] |= (uint8_t)bit; marked += bit; }
    return marked;
}

/* Pantalla ancha: relleno del arte en los extremos de la cámara (D54).  El juego carga el arte de cada escenario en el
 * tilemap de SCROLL2 y SCROLL3 (anillo de 64 columnas) y no lo cambia al mover la cámara; las columnas que sobran quedan
 * transparentes.  Con la vista ensanchada la cámara, en un extremo, asoma por ese hueco.  art_run_find busca el tramo más
 * largo de columnas del anillo sin ningún tile visible (lo que sobra del anillo); fill_row_cols, para una fila de tiles,
 * lo ensancha con las columnas contiguas que en esa fila también están vacías (el suelo suele acabar antes que el techo)
 * y hace que cada columna del tramo tome, reflejada, la columna de arte más cercana al borde que tiene a su lado.
 * Devuelve 1 si hay algo que rellenar. */
/* 1 si todos los píxeles opacos del tile (con la paleta del cuadro) son casi negros: relleno oscuro del anillo que no es escenario, como las barras de Balrog o
 * la pared oscura a la izquierda de Zangief.  Se memoriza por (código, color) y por cuadro. */
#define DARK_MAX 48
static int tile_dark(Video *v, int layer, int code, int attr) {
    const int cb = 0x200 * layer + (attr & 0x1f) * 16;
    const uint32_t key = ((uint32_t)code << 12) | (uint32_t)cb | 0x80000000u, h = (key * 2654435761u) >> 20;
    if (v->dk_key[h] == key)
        return v->dk_val[h];
    const int tile = gfx_map_fast(v->gfx, layer == 2 ? GFXTYPE_SCROLL2 : GFXTYPE_SCROLL3, code), n = layer == 2 ? 16 : 32;
    int dark = tile >= 0;
    for (int y = 0; dark && y < n; y++) {
        const uint8_t *r = layer == 2 ? gfx_row16(v->gfx, tile, y) : gfx_row32(v->gfx, tile, y);
        for (int x = 0; x < n; x++) {
            if (r[x] == 15) continue;
            const uint32_t c = v->rgb[cb + r[x]];
            if (((c >> 16) & 255) > DARK_MAX || ((c >> 8) & 255) > DARK_MAX || (c & 255) > DARK_MAX) { dark = 0; break; }
        }
    }
    v->dk_key[h] = key;
    v->dk_val[h] = (uint8_t)dark;
    return dark;
}

/* 1 si el tile no cuenta como arte al buscar dónde acaba el escenario: es transparente o, en los escenarios que rellenan con tiles oscuros fuera de su arte
 * (Balrog, Zangief), es de ese relleno */
static inline int art_blank(Video *v, int layer, int code, int attr) {
    if (v->gfx->blank[layer - 2][code])
        return 1;
    return (v->stage_id == STAGE_BALROG || v->stage_id == STAGE_ZANGIEF) && tile_dark(v, layer, code, attr);
}

static int art_run_find(Video *v, const Mem *m, int layer, uint32_t base, int *run_start, int *run_len) {
    uint8_t used[64], full[64];
    int nused = 0;
    /* las filas que tienen arte en casi todo el anillo (el suelo, que da la vuelta entera) no dicen dónde acaba el arte: se ignoran */
    for (int row = 0; row < 64; row++) {
        int n = 0;
        for (int col = 0; col < 64; col++) {
            const uint32_t off = base + 4u * (uint32_t)scan_index(layer, col, row);
            int code = gfx_word(m, off);
            if (layer == 3) code &= 0x3fff;
            n += !art_blank(v, layer, code, gfx_word(m, off + 2));
        }
        full[row] = n >= 60;
    }
    for (int col = 0; col < 64; col++) {
        used[col] = 0;
        for (int row = 0; row < 64 && !used[col]; row++) {
            if (full[row]) continue;
            const uint32_t off = base + 4u * (uint32_t)scan_index(layer, col, row);
            int code = gfx_word(m, off);
            if (layer == 3) code &= 0x3fff;
            used[col] = !art_blank(v, layer, code, gfx_word(m, off + 2));
        }
        nused += used[col];
    }
    if (nused == 0 || nused == 64)
        return 0;
    *run_start = -1;
    *run_len = 0;
    for (int c = 0; c < 64; c++) {
        if (used[c] || !used[(c + 63) & 63])
            continue;                                   /* no es el principio de un tramo vacío */
        int len = 0;
        while (!used[(c + len) & 63]) len++;
        if (len > *run_len) { *run_len = len; *run_start = c; }
    }
    return 1;
}

#define FILL_NEAR 8      /* tiles de SCROLL2 (128 px) a los que un corte de una fila de poco arte se considera borde del escenario */

static void fill_row_cols(int layer, int run_start, int run_len, const uint8_t blank_row[64], int8_t src[64]) {
    int used_in_row = 0;
    for (int col = 0; col < 64; col++) {
        used_in_row += !blank_row[col];
        src[col] = -1;
    }
    int start = run_start, len = run_len, in_end = 0, in_start = 0;
    if (used_in_row * 2 >= 64 - run_len) {              /* fila con arte de verdad: su borde puede estar más adentro */
        while (len < 63 && blank_row[(start + 63) & 63]) { start = (start + 63) & 63; len++; in_end++; }
        while (len < 63 && blank_row[(start + len) & 63]) { len++; in_start++; }
    } else if (layer == 2) {
        /* fila con poco arte (un tejado, una pared a medias): si su arte empieza o acaba a pocos tiles del borde del escenario, el borde es un corte y no un
         * objeto suelto; se extiende el tramo hasta ahí para reflejarlo (de lo contrario queda un hueco entre el borde del escenario y el de la fila) */
        int a = 0, b = 0;
        while (a < FILL_NEAR && blank_row[(start + 63 - a) & 63]) a++;
        if (a > 0 && a < FILL_NEAR && !blank_row[(start + 63 - a) & 63]) { start = (start + 64 - a) & 63; len += a; in_end += a; }
        while (b < FILL_NEAR && blank_row[(start + len + b) & 63]) b++;
        if (b > 0 && b < FILL_NEAR && !blank_row[(start + len + b) & 63]) { len += b; in_start += b; }
    }
    const int a1 = (start + 63) & 63, a0 = (start + len) & 63, art = 64 - len;
    for (int k = 0; k < len; k++) {
        const int d1 = k + 1, d0 = len - k;             /* distancia al último y al primer tile de arte */
        const int d = d1 <= d0 ? d1 : d0;
        if (d > art || !blank_row[(start + k) & 63])
            continue;
        /* capa lejana: si el arte de esta fila acaba bastante antes que el del escenario (la cubierta de un barco, un tejado), lo que
         * hay más allá era cielo o mar en la recreativa; reflejarlo pegaría un trozo suelto, así que se deja vacío */
        if (layer == 3 && (d1 <= d0 ? in_end : in_start) >= 2)
            continue;
        /* y en la capa lejana el reflejo no pasa de 2 tiles (64 px, el margen del widescreen de 512): más allá repetiría árboles, gente y casas
         * como si fueran escenario, y la capa de detrás (el suelo, el cielo) queda a la vista */
        if (layer == 3 && d > 2)
            continue;                                   /* más lejos que el ancho del arte: no hay qué reflejar */
        src[(start + k) & 63] = (int8_t)(d1 <= d0 ? (a1 - (d1 - 1)) & 63 : (a0 + (d0 - 1)) & 63);
    }
}

/* 1 si el tile no tiene ningún píxel transparente (pen 15), es decir, es fondo y no la silueta de un objeto */
static int tile_solid(const Video *v, int gfxtype, int code) {
    const int tile = gfx_map_fast(v->gfx, gfxtype, code);
    if (tile < 0)
        return 0;
    for (int y = 0; y < 16; y++) {
        const uint8_t *r = gfx_row16(v->gfx, tile, y);
        for (int x = 0; x < 16; x++)
            if (r[x] == 15)
                return 0;
    }
    return 1;
}

/* 1 si entre el tile `src` y los 3 de cada lado de su fila hay alguno con píxeles transparentes (la silueta de un objeto: ala, casco): el reflejo de un borde así
 * deja trozos sueltos (aletas, misiles, cascos cortados), aunque el tile del borde en sí sea sólido */
static int silhouette_near(const Video *v, int gfxtype, const uint8_t blank_row[64], const int row_code[64], int8_t memo[64], int src) {
    for (int k = -3; k <= 3; k++) {
        const int c = (src + k) & 63;
        if (blank_row[c])
            continue;
        if (memo[c] < 0)
            memo[c] = (int8_t)tile_solid(v, gfxtype, row_code[c]);
        if (!memo[c])
            return 1;
    }
    return 0;
}

/* Dibuja la capa `layer` (1-3).  high = 0: pase normal (pens distintos de 15, sin tocar la prioridad);
 * high = 1: pase de prioridad (marca el bit 0 de pri en los pens que la máscara del grupo del tile declara
 * por delante de los sprites, incluido el 15 si la máscara lo tiene), sin dibujar.  Se instancia con layer y high
 * constantes (render_tilemap), así el tamaño de tile también lo es. */
static SF2_ALWAYS_INLINE void tilemap_impl(Video *v, const Mem *m, const int layer, const int high) {
    const int tshift = layer == 1 ? 3 : layer == 2 ? 4 : 5;     /* 8, 16, 32 píxeles por tile */
    const int ts = 1 << tshift;
    const int wmask = (64 << tshift) - 1;                       /* 512, 1024, 2048 píxeles de ancho y alto */
    const int rowstride = layer == 3 ? 32 : 16;                 /* bytes de pens entre dos filas del tile */
    const uint32_t base = gfx_base(m, CPSA_SCROLL1_BASE + (layer - 1), 0x4000);   /* bases consecutivas: 1, 2, 3 */
    const int scrollx = v->st->scrollx[layer - 1];
    const int scrolly = v->st->scrolly[layer - 1];
    const int colorbase = 0x200 * layer;
    const int gfxtype = layer == 1 ? GFXTYPE_SCROLL1 : layer == 2 ? GFXTYPE_SCROLL2 : GFXTYPE_SCROLL3;
    const int rowscroll = layer == 2 && v->rowscroll;
    const int otheroffs = v->st->rowoffs;
    uint16_t pmask[4];
    for (int g = 0; g < 4; g++) pmask[g] = m->cps_b[CPSB11_PRIORITY0 + g];
    TileInfo info[64];
    int info_row = -1;
    const int rw = v->rw, rx0 = v->rx0;
    int8_t fill[64];                    /* columna de origen de cada columna rellenada en la fila de tiles actual, o -1 */
    int run_start = 0, run_len = 0;
    const int has_fill = layer >= 2 && rw > VIDEO_W && art_run_find(v, m, layer, base, &run_start, &run_len);
    uint8_t blank_row[64];
    int row_code[64];                   /* código de tile de cada columna de la fila actual */
    int8_t solid_memo[64];              /* tile_solid de cada columna de la fila actual: -1 sin calcular */

    for (int py = 0; py < VIDEO_H; py++) {
        const int Y = py + VIDEO_RAW_Y0;
        int sx = scrollx;
        if (rowscroll)
            sx += v->st->rows[(Y + otheroffs) & 0x3ff];
        const int ty = (Y + scrolly) & wmask;
        const int row = ty >> tshift, yin = ty & (ts - 1);
        if (row != info_row) {
            /* los 64 tiles de esta fila del tilemap: la RAM gráfica no cambia durante el render */
            for (int col = 0; col < 64; col++) {
                const int idx = scan_index(layer, col, row);
                const uint32_t off = base + 4u * (uint32_t)idx;
                int code = gfx_word(m, off);
                const uint16_t attr = gfx_word(m, off + 2);
                if (layer == 3) code &= 0x3fff;
                if (has_fill) { blank_row[col] = (uint8_t)art_blank(v, layer, code, attr); row_code[col] = code; }
                const int tile = gfx_map_fast(v->gfx, gfxtype, code);
                TileInfo *t = &info[col];
                t->flipx = (attr >> 5) & 1;
                t->flipy = (attr >> 6) & 1;
                t->cb = (uint16_t)(colorbase + (attr & 0x1f) * 16);
                t->mask = pmask[(attr >> 7) & 3];
                if (tile < 0) t->pens = NULL;
                else if (layer == 1) t->pens = gfx_row8(v->gfx, tile, 0, (idx >> 5) & 1);
                else if (layer == 2) t->pens = gfx_row16(v->gfx, tile, 0);
                else t->pens = gfx_row32(v->gfx, tile, 0);
            }
            if (has_fill) {
                memset(solid_memo, -1, sizeof solid_memo);
                fill_row_cols(layer, run_start, run_len, blank_row, fill);
                for (int col = 0; col < 64; col++)
                    if (fill[col] >= 0) {
                        /* SCROLL2: un tile con píxeles transparentes (la silueta de un avión, una persona) cortado por el borde del arte no se refleja: su
                         * reflejo deja trozos sueltos (aletas y misiles en el avión de Guile); se deja vacío y se ve lo que hay detrás */
                        if (layer == 2 && (v->stage_id == STAGE_GUILE || v->stage_id == STAGE_KEN) && silhouette_near(v, gfxtype, blank_row, row_code, solid_memo, fill[col])) {
                            fill[col] = -1;
                            continue;
                        }
                        info[col] = info[(int)fill[col]];
                        info[col].flipx ^= 1;
                    }
            }
            info_row = row;
        }
        uint16_t *pix = v->pix + py * rw;
        uint8_t *pri = v->pri + py * rw;
        int px = 0;
        while (px < rw) {
            const int tx = (px + rx0 + sx) & wmask;
            const int col = tx >> tshift, xin = tx & (ts - 1);
            int n = ts - xin;
            if (px + n > rw) n = rw - px;
            if (has_fill && fill[col] >= 0) {
                /* columna rellenada: solo cuenta fuera de la ventana de la recreativa (coordenadas crudas 64-447), dentro
                 * es el hueco original y el cuadro de 384 queda idéntico */
                const int raw = px + rx0;
                if (raw < VIDEO_RAW_X0 && raw + n > VIDEO_RAW_X0) n = VIDEO_RAW_X0 - raw;
                else if (raw >= VIDEO_RAW_X0 && raw < VIDEO_RAW_X0 + VIDEO_W) {
                    if (raw + n > VIDEO_RAW_X0 + VIDEO_W) n = VIDEO_RAW_X0 + VIDEO_W - raw;
                    px += n;
                    continue;
                }
            }
            const TileInfo *t = &info[col];
            const uint16_t mask = t->mask;
            if (!t->pens) {
                /* tile fuera del mapeador: todo pen 15 (transparente; en el pase de prioridad cuenta si la máscara lo tiene) */
                if (high && (mask >> 15) & 1) {
                    for (int i = 0; i < n; i++) pri[px + i] |= 1;
                    v->marked += n;
                }
                px += n;
                continue;
            }
            const int yy = t->flipy ? ts - 1 - yin : yin;
            const uint8_t *rowpens = t->pens + yy * rowstride;
            const uint8_t *src = rowpens + (t->flipx ? ts - 1 - xin : xin);
            if (high) {
                if (n == ts) v->marked += span_mark(pri + px, src, ts, mask, t->flipx);
                else v->marked += span_mark(pri + px, src, n, mask, t->flipx);
            } else {
                if (n == ts) span_draw(pix + px, src, ts, t->cb, t->flipx);
                else span_draw(pix + px, src, n, t->cb, t->flipx);
            }
            px += n;
        }
    }
}

static void render_tilemap(Video *v, const Mem *m, int layer, int high) {
    if (!v->enabled[layer] || (layer != 2 && v->hide_far))
        return;
    switch (layer * 2 + high) {
    case 2: tilemap_impl(v, m, 1, 0); break;
    case 3: tilemap_impl(v, m, 1, 1); break;
    case 4: tilemap_impl(v, m, 2, 0); break;
    case 5: tilemap_impl(v, m, 2, 1); break;
    case 6: tilemap_impl(v, m, 3, 0); break;
    default: tilemap_impl(v, m, 3, 1); break;
    }
}

/* ---- sprites --------------------------------------------------------------------------- */
static void draw_sprite_tile(Video *v, int tile, int col, int flipx, int flipy, int sx, int sy) {
    const int cb = col * 16;
    /* recorte a la zona visible (sin envolver aquí: el reaparecer por la izquierda lo pide draw_sprite_tile_wrap) */
    int ty0 = VIDEO_RAW_Y0 - sy, ty1 = VIDEO_H + VIDEO_RAW_Y0 - sy;
    const int rw = v->rw, rx0 = v->rx0;
    int tx0 = rx0 - sx, tx1 = rw + rx0 - sx;
    if (ty0 < 0) ty0 = 0;
    if (ty1 > 16) ty1 = 16;
    if (tx0 < 0) tx0 = 0;
    if (tx1 > 16) tx1 = 16;
    for (int ty = ty0; ty < ty1; ty++) {
        const int Y = sy + ty - VIDEO_RAW_Y0;
        const uint8_t *rowpens = gfx_row16(v->gfx, tile, flipy ? 15 - ty : ty);
        uint16_t *pix = v->pix + Y * rw;
        uint8_t *pri = v->pri + Y * rw;
        for (int tx = tx0; tx < tx1; tx++) {
            const int X = sx + tx - rx0;
            const int pen = rowpens[flipx ? 15 - tx : tx];
            if (pen == 15) continue;
            /* prio_transpen con pmask 0x02 (y el bit 31 implícito que añade gfx_element::prio_transpen): el
             * píxel se dibuja salvo que la prioridad valga 1 (tile por delante de los sprites) o 31 (ya lo
             * escribió un sprite anterior); después la prioridad pasa a 31.  Así la primera entrada de la
             * tabla queda encima de las siguientes: en el CPS1 el índice más bajo tiene la prioridad más alta. */
            if (((1u << (pri[X] & 0x1f)) & 0x80000002u) == 0)
                pix[X] = (uint16_t)(cb + pen);
            else
                v->blocked++;
            pri[X] = 31;
        }
    }
}

/* Con la ventana ensanchada (rw > 384) la X de 9 bits da la vuelta dentro de la propia ventana.  Los tiles que pasan de la X 512
 * no reaparecen por la izquierda: un luchador que salta contra la pared derecha dejaba trozos de sprite en el borde izquierdo
 * (en la placa nunca se veían, la ventana de 384 queda lejos de la vuelta); se recortan en el borde derecho. */
static void draw_sprite_tile_wrap(Video *v, int tile, int col, int flipx, int flipy, int sx, int sy) {
    draw_sprite_tile(v, tile, col, flipx, flipy, sx, sy);
}

/* Barras de vida con hud = edges: cada barra son 10 sprites de 16 px en ranuras consecutivas (44-53 la del 1P, 54-63 la del 2P): el
 * remate del extremo exterior y 9 tiles de cuerpo (144 px).  Al pegarlas a los bordes de la ventana ancha quedaba un hueco hasta
 * el centro; aquí se compone la barra en un búfer de 160 px, el remate pasa tal cual y el cuerpo se estira en horizontal para
 * que la barra llegue hasta donde estaba su extremo interior, junto al KO y el tiempo.  Devuelve 0 si las ranuras no tienen la
 * forma esperada (entonces se dibujan como siempre). */
#define BAR_LEN 160
static int draw_hud_bar(Video *v, int first, int p2, int shift) {
    static uint16_t buf[16 * BAR_LEN];
    const int x0 = p2 ? 272 : 80;                       /* X cruda del extremo interior (2P) o exterior (1P) de la barra original */
    int yref = -1;
    for (int k = 0; k < 16 * BAR_LEN; k++) buf[k] = 0xffff;
    for (int n = 0; n < 10; n++) {
        const int i = (first + n) * 4;
        const int x = v->xsigned ? (int16_t)v->st->obj[i] : v->st->obj[i], y = v->st->obj[i + 1] & 0x1ff;
        const int colour = v->st->obj[i + 3];
        if (v->st->obj[i + 2] == 0 && colour == 0)
            continue;
        if ((colour & 0xff00) || x < x0 || x > x0 + BAR_LEN - 16 || y >= 80)
            return 0;
        if (yref < 0) yref = y;
        if (y != yref)
            return 0;
        const int tile = gfx_map_fast(v->gfx, GFXTYPE_SPRITES, v->st->obj[i + 2]);
        if (tile < 0)
            continue;
        const int col = colour & 0x1f, flipx = (colour >> 5) & 1, flipy = (colour >> 6) & 1;
        for (int ty = 0; ty < 16; ty++) {
            const uint8_t *rowpens = gfx_row16(v->gfx, tile, flipy ? 15 - ty : ty);
            for (int tx = 0; tx < 16; tx++) {
                const int pen = rowpens[flipx ? 15 - tx : tx];
                uint16_t *d = &buf[ty * BAR_LEN + (x - x0) + tx];
                if (pen != 15 && *d == 0xffff) *d = (uint16_t)(col * 16 + pen);
            }
        }
    }
    if (yref < 0)
        return 1;
    /* destino en X cruda: el 1P empieza `shift` más a la izquierda y acaba donde acababa; el 2P empieza donde empezaba y acaba `shift` más a la derecha */
    const int dx0 = p2 ? x0 : x0 - shift, dw = BAR_LEN + shift, body = 144 + shift;
    const int cap_src = p2 ? 144 : 0, cap_dst = p2 ? dw - 16 : 0;           /* el remate: 16 px del extremo exterior */
    const int body_src = p2 ? 0 : 16, body_dst = p2 ? 0 : 16;
    for (int ty = 0; ty < 16; ty++) {
        const int Y = yref + ty - VIDEO_RAW_Y0;
        if (Y < 0 || Y >= VIDEO_H)
            continue;
        uint16_t *pix = v->pix + Y * v->rw;
        uint8_t *pri = v->pri + Y * v->rw;
        for (int d = 0; d < dw; d++) {
            int sx;
            if (d >= cap_dst && d < cap_dst + 16) sx = cap_src + (d - cap_dst);
            else if (d >= body_dst && d < body_dst + body) sx = body_src + (d - body_dst) * 144 / body;
            else continue;
            const uint16_t c = buf[ty * BAR_LEN + sx];
            const int X = dx0 + d - v->rx0;
            if (c == 0xffff || X < 0 || X >= v->rw)
                continue;
            if (((1u << (pri[X] & 0x1f)) & 0x80000002u) == 0) pix[X] = c;
            else v->blocked++;
            pri[X] = 31;
        }
    }
    return 1;
}

static void render_sprites(Video *v) {
    /* find_last_sprite: la marca de fin es un atributo con el byte alto 0xFF; sin marca, toda la tabla */
    int last = OBJ_WORDS - 4;
    for (int off = 0; off < OBJ_WORDS; off += 4) {
        if ((v->st->obj[off + 3] & 0xff00) == 0xff00) { last = off - 4; break; }
    }
    v->sprites = 0;
    /* cps1_render_sprites recorre la tabla desde la primera entrada; con la regla de prioridad de
     * draw_sprite_tile, la primera entrada dibujada es la que queda encima */
    const int hud_shift = v->hud_edges ? (v->rw - VIDEO_W) / 2 : 0;
    int bar_done[2] = { 0, 0 };
    for (int i = 0; i <= last; i += 4) {
        if (hud_shift && i >= 44 * 4 && i < 64 * 4) {
            const int p2 = i >= 54 * 4;
            if (i == (p2 ? 54 : 44) * 4)
                bar_done[p2] = draw_hud_bar(v, p2 ? 54 : 44, p2, hud_shift);
            if (bar_done[p2])
                continue;
        }
        int x = v->xsigned ? (int16_t)v->st->obj[i] : v->st->obj[i];
        const int y = v->st->obj[i + 1];
        const int colour = v->st->obj[i + 3];
        if (hud_shift) {
            x += hud_side(i / 4, x + 8 * (((colour & 0x0f00) >> 8) + 1), y & 0x1ff, v->st->obj[i + 2]) * hud_shift;
            if (!v->xsigned) x &= 0x1ff;
        }
        const int col = colour & 0x1f, flipx = (colour >> 5) & 1, flipy = (colour >> 6) & 1;
        const int code = gfx_map_fast(v->gfx, GFXTYPE_SPRITES, v->st->obj[i + 2]);
        if (code < 0) continue;
        v->sprites++;
        if (colour & 0xff00) {
            const int nx = ((colour & 0x0f00) >> 8) + 1, ny = ((colour & 0xf000) >> 12) + 1;
            for (int nys = 0; nys < ny; nys++) {
                const int sy = (y + nys * 16) & 0x1ff;
                const int rowcode = 0x10 * (flipy ? ny - 1 - nys : nys);
                for (int nxs = 0; nxs < nx; nxs++) {
                    const int sx = v->xsigned ? x + nxs * 16 : (x + nxs * 16) & 0x1ff;
                    const int colcode = flipx ? (code + (nx - 1) - nxs) & 0xf : (code + nxs) & 0xf;
                    draw_sprite_tile_wrap(v, (code & ~0xf) + colcode + rowcode, col, flipx, flipy, sx, sy);
                }
            }
        } else {
            draw_sprite_tile_wrap(v, code, col, flipx, flipy, v->xsigned ? x : x & 0x1ff, y & 0x1ff);
        }
    }
}

/* ---- cuadro ---------------------------------------------------------------------------- */
static void render_layer(Video *v, const Mem *m, int layer) {
    if (layer == 0) { if (v->enabled[0]) render_sprites(v); }
    else render_tilemap(v, m, layer, 0);
}

static void render_high_layer(Video *v, const Mem *m, int layer) {
    if (layer != 0) render_tilemap(v, m, layer, 1);     /* no hay sprites de alta prioridad */
}

/* Instantánea del cuadro (HFR, docs/HFR.md).  VideoSnap guarda lo que el HFR interpolará: la tabla de objetos latcheada, la X/Y de las tres capas y el
 * desplazamiento de filas.  Además se congela todo lo que el dibujado lee de la máquina (RAM gráfica, paleta, registros de CPS-A y CPS-B, y la parte
 * de la RAM de trabajo que decide la escena) en v->fz, porque el manejador de IRQ2 corre dentro del propio VBlank, antes de que sf2_run_frame
 * devuelva el control, y cambia esos registros y la RAM para el cuadro siguiente. */
static void snap_capture(Video *v, const Mem *m, VideoSnap *st) {
    v->fz = *m;                                         /* copia superficial: los campos que se usan se redirigen a las copias propias */
    memcpy(v->fz_gfxram, m->gfxram, GFXRAM_SIZE);
    memcpy(v->fz_wram + FZ_WRAM_LO, m->wram + FZ_WRAM_LO, FZ_WRAM_LEN);
    v->fz.gfxram = v->fz_gfxram;
    v->fz.wram = v->fz_wram;
    memcpy(st->obj, v->obj, sizeof st->obj);
    for (int k = 0; k < 3; k++) {
        st->scrollx[k] = m->cps_a[CPSA_SCROLL1_X + 2 * k];
        st->scrolly[k] = m->cps_a[CPSA_SCROLL1_Y + 2 * k];
    }
    st->rowoffs = m->cps_a[CPSA_ROWSCROLL_OFFS];
    const uint32_t other = gfx_base(m, CPSA_OTHER_BASE, 0x800);
    for (int i = 0; i < 0x400; i++)
        st->rows[i] = gfx_word(m, other + 2u * (uint32_t)i);
}

void video_render(Video *v, const Mem *m) {
    snap_capture(v, m, &v->cur);
    if (!v->in_vblank)
        v->prev = v->cur;                /* re-dibujado del anfitrión (cambio de modo, carga de estado): no hay cuadro anterior que interpolar */
    video_render_snap(v, &v->cur);
}

/* Huecos estrechos fuera de la ventana de la recreativa: tiles transparentes en el borde del arte de todas las capas dejan una rendija sin dibujar (el pen del
 * fondo) entre dos tramos de escenario, como la que separa los estandartes de Honda.  Se rellena con el píxel vecino de la propia fila; los huecos anchos (las
 * barras negras de Balrog, donde no hay escenario) se dejan como están. */
#define HOLE_MAX 24
static void fill_narrow_holes(Video *v) {
    const int rw = v->rw, lo = VIDEO_RAW_X0 - v->rx0, hi = lo + VIDEO_W;      /* columnas de la ventana original: [lo, hi) */
    for (int y = 0; y < VIDEO_H; y++) {
        uint16_t *pix = v->pix + y * rw;
        const uint8_t *pri = v->pri + y * rw;
        for (int x = 0; x < rw;) {
            if (pix[x] != VIDEO_BACKDROP_PEN || pri[x] || (x >= lo && x < hi)) { x++; continue; }
            int e = x;
            while (e < rw && pix[e] == VIDEO_BACKDROP_PEN && !pri[e] && !(e >= lo && e < hi)) e++;
            if (e - x <= HOLE_MAX && (x > 0 || e < rw)) {
                const uint16_t c = x > 0 ? pix[x - 1] : pix[e];
                for (int k = x; k < e; k++) pix[k] = c;
            }
            x = e;
        }
    }
}

void video_render_snap(Video *v, const VideoSnap *st) {
    const Mem *m = &v->fz;
    v->st = st;
    if (!v->rgb_fixed)
        build_rgb(v, m);
    const uint16_t vc = m->cps_a[CPSA_VIDEOCONTROL];
    const uint16_t lc = m->cps_b[CPSB11_LAYER_CONTROL];
    if (vc & 0x8000)
        v->flip_warned = 1;            /* volteo de pantalla pedido por el juego, no implementado (D24): lo informa el anfitrión */
    v->enabled[0] = 1;
    v->enabled[1] = (lc & CPSB11_LAYER1_ENABLE) != 0;
    v->enabled[2] = (lc & CPSB11_LAYER2_ENABLE) && (vc & 0x04);
    v->enabled[3] = (lc & CPSB11_LAYER3_ENABLE) && (vc & 0x08);
    if (v->layer_mask != 0xf)              /* depuración: solo las capas pedidas (SF2_VIDEO_LAYERS) */
        for (int k = 0; k < 4; k++) if (!((v->layer_mask >> k) & 1)) v->enabled[k] = 0;
    v->rowscroll = vc & 1;
    /* stage completo contra Dhalsim (personaje 7 en 0xFF8668): SCROLL3 trae el cuerpo de los dos elefantes de primer plano y SCROLL1 su cabeza y
     * trompa; con la cámara fija quedan cortados contra el borde del arte y se quitan (los elefantes del fondo siguen en SCROLL2) */
    memset(v->dk_key, 0, sizeof v->dk_key);              /* la paleta puede haber cambiado */
    v->stage_id = m->wram[0x8668];                      /* personaje del oponente = escenario (0 Ryu, 1 Honda, 2 Blanka, 3 Guile, 4 Ken, 5 Chun-Li, 6 Zangief, 7 Dhalsim, 8 Bison, 9 Sagat, 10 Balrog, 11 Vega) */
    v->hide_far = v->w > VIDEO_W && video_scene_wide(m) && m->wram[0x8668] == STAGE_DHALSIM;
    for (int k = 0; k < 4; k++) v->order[k] = (lc >> (6 + 2 * k)) & 3;

    /* geometría de este cuadro: ensanchado solo si el anfitrión lo pidió y la pantalla lo admite */
    const int wide = v->w > VIDEO_W && video_scene_wide(m);
    v->rw = wide ? v->w : VIDEO_W;
    v->rx0 = VIDEO_RAW_X0 - (v->rw - VIDEO_W) / 2;
    const int npix = v->rw * VIDEO_H;
    for (int i = 0; i < npix; i++) v->pix[i] = VIDEO_BACKDROP_PEN;
    memset(v->pri, 0, (size_t)npix);
    v->marked = v->blocked = 0;

    /* render_layers: la capa dibujada justo antes de los sprites prepara la máscara de prioridad */
    for (int k = 0; k < 4; k++) {
        render_layer(v, m, v->order[k]);
        if (k < 3 && v->order[k + 1] == 0)
            render_high_layer(v, m, v->order[k]);
    }
    if (v->rw > VIDEO_W)
        fill_narrow_holes(v);
    if (v->rw == v->w) {
        for (int i = 0; i < npix; i++) v->frame[i] = v->rgb[v->pix[i]];
    } else {
        /* escena de 384 en un cuadro más ancho (D57): centrada; a cada lado, las barras toman el color del fondo de la pantalla
         * (el del borde en la fila de arriba: el azul del mapa, el negro del VS y del final) */
        const int off = (v->w - v->rw) / 2;
        for (int y = 0; y < VIDEO_H; y++) {
            const uint16_t *row = v->pix + y * v->rw;
            uint32_t *dst = v->frame + y * v->w;
            for (int x = 0; x < v->rw; x++)
                dst[off + x] = v->rgb[row[x]];
            const uint32_t cl = v->rgb[v->pix[0]], cr = v->rgb[v->pix[v->rw - 1]];      /* el color del fondo: el de la fila de arriba */
            for (int x = 0; x < off; x++) {
                dst[x] = cl;
                dst[off + v->rw + x] = cr;
            }
        }
    }
    v->renders++;
}

/* ---- HFR: cuadros intermedios ---------------------------------------------------------- */
#define HFR_SCROLL_MAX 64   /* salto de una capa por encima del cual es un corte de escena y no se interpola */
#define HFR_ROW_MAX    32   /* el de una línea de rowscroll */
#define HFR_SPRITE_MAX 12   /* y el de un sprite: lo normal son 1-4 px por cuadro y en el 98 % de los casos menos de 10.  Más es un salto de golpe o una
                             * ranura reasignada: una hilera de tiles iguales (el borde de la barda del stage de Ryu) se corre de 16 en 16 px de un cuadro
                             * al otro y, emparejada por ranura, se vería deslizarse y parpadear */

static inline int hfr_step(int d, int t256) { return (d * t256 + 128) >> 8; }

static uint16_t hfr_lerp16(uint16_t a, uint16_t b, int t256, int max) {
    const int d = (int16_t)(uint16_t)(b - a);
    return abs(d) > max ? b : (uint16_t)(a + hfr_step(d, t256));
}

void video_render_t(Video *v, int t256) {
    if (t256 >= 256) {
        video_render_snap(v, &v->cur);
        return;
    }
    if (t256 < 0) t256 = 0;
    const VideoSnap *p = &v->prev, *c = &v->cur;
    VideoSnap *m = &v->mid;
    *m = *c;
    for (int k = 0; k < 3; k++) {
        m->scrollx[k] = hfr_lerp16(p->scrollx[k], c->scrollx[k], t256, HFR_SCROLL_MAX);
        m->scrolly[k] = hfr_lerp16(p->scrolly[k], c->scrolly[k], t256, HFR_SCROLL_MAX);
    }
    if (p->rowoffs == c->rowoffs)
        for (int i = 0; i < 0x400; i++)
            m->rows[i] = hfr_lerp16(p->rows[i], c->rows[i], t256, HFR_ROW_MAX);
    for (int off = 0; off < OBJ_WORDS; off += 4) {
        if ((c->obj[off + 3] & 0xff00) == 0xff00)
            break;                                              /* marca de fin de la tabla (find_last_sprite) */
        const uint16_t *pe = p->obj + off, *ce = c->obj + off;
        if (pe[2] != ce[2] || pe[3] != ce[3] || (ce[2] == 0 && ce[3] == 0))
            continue;                                           /* otra forma o ranura vacía: se queda el valor actual */
        int dx, dy;
        if (v->xsigned) dx = (int16_t)ce[0] - (int16_t)pe[0];
        else dx = (((ce[0] & 0x1ff) - (pe[0] & 0x1ff) + 256) & 0x1ff) - 256;
        dy = (((ce[1] & 0x1ff) - (pe[1] & 0x1ff) + 256) & 0x1ff) - 256;
        if (abs(dx) > HFR_SPRITE_MAX || abs(dy) > HFR_SPRITE_MAX)
            continue;
        m->obj[off] = v->xsigned ? (uint16_t)((int16_t)pe[0] + hfr_step(dx, t256)) : (uint16_t)(((pe[0] & 0x1ff) + hfr_step(dx, t256)) & 0x1ff);
        m->obj[off + 1] = (uint16_t)((ce[1] & ~0x1ff) | (((pe[1] & 0x1ff) + hfr_step(dy, t256)) & 0x1ff));
    }
    video_render_snap(v, m);
}

void video_vblank(Video *v, const Mem *m) {
    v->prev = v->cur;                    /* el cuadro que se acaba de mostrar pasa a ser el anterior (HFR) */
    v->in_vblank = 1;
    video_render(v, m);
    v->in_vblank = 0;
    video_latch_obj(v, m);
}

/* ---- PNG ------------------------------------------------------------------------------- */
int video_encode_png(const Video *v, uint8_t **png, size_t *len) {
    return png_encode_rgb(v->frame, v->w, VIDEO_H, png, len);
}

void video_state(Video *v, StateStream *s) {
    st_u16s(s, v->obj, OBJ_WORDS);
}
