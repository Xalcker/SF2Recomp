/*
 * video.h - vídeo CPS-A / CPS-B-11 de SF2 (hito 3): paleta, tres capas de scroll, sprites y prioridades,
 * calcado de cps1_v.cpp de MAME 0.289 para que el cuadro coincida píxel a píxel.
 *
 * Pantalla: 512 x 262 ciclos de píxel (8 MHz), zona visible 384 x 224 que empieza en la coordenada
 * (64, 16) del contador de la placa (CPS_HBEND, CPS_VBEND).  Los sprites y los scrolls trabajan en esas
 * coordenadas "crudas": un sprite en x = 64, y = 16 aparece en la esquina superior izquierda.
 *
 * Secuencia por cuadro (vblank_begin de MAME): (1) se renderiza el cuadro con los registros y la RAM
 * gráfica de ese instante y con la tabla de objetos copiada en el VBlank anterior; (2) se copia la tabla
 * de objetos (0x800 bytes en la base OBJ) para el cuadro siguiente; (3) se entrega la IRQ2.  Por eso el
 * manejador de IRQ2, que escribe los registros de vídeo, afecta al cuadro siguiente, y los sprites van
 * con un cuadro de retraso respecto a la RAM (doble búfer del juego: el manejador alterna la base OBJ).
 *
 * Capas: control de capas 0x800166, bits 6-7, 8-9, 10-11, 12-13 = capa dibujada en 1.º, 2.º, 3.º y 4.º
 * lugar (0 = sprites, 1-3 = SCROLL1-3).  Habilitación: bits 0x08/0x10/0x20 del control de capas (y bits
 * 2 y 3 del control de vídeo 0x800122 para SCROLL2 y SCROLL3).  Las máscaras de prioridad 0x800168-
 * 0x80016E indican, para los tiles del grupo 0-3 (bits 7-8 del atributo), qué pens de la capa dibujada
 * justo antes de los sprites quedan por delante de ellos.  Pen 15 es transparente en tiles y sprites.
 *
 * No implementado: la capa de estrellas (SF2 no la usa; el juego ni la habilita ni tiene ROM de
 * estrellas) y el volteo de pantalla (bit 15 del control de vídeo; el dipswitch por defecto no lo activa
 * y el juego nunca lo escribe en la partida de referencia).  Si el juego lo activara se avisa una vez.
 */
#ifndef RECOMP_VIDEO_H
#define RECOMP_VIDEO_H

#include <stdint.h>
#include <stddef.h>
#include "mem.h"
#include "gfx.h"
#include "png.h"

#define VIDEO_W      384            /* ancho de la recreativa: lo que se compara con MAME */
#define VIDEO_W_MAX  800            /* techo del modo ancho: con el stage completo la X de los sprites se guarda con signo (16 bits) y no hay más límite que el anillo de SCROLL2 (1024) */
#define VIDEO_H      224
#define VIDEO_RAW_X0 64
#define VIDEO_RAW_Y0 16
#define VIDEO_PIXELS (VIDEO_W * VIDEO_H)
#define VIDEO_PIXELS_MAX (VIDEO_W_MAX * VIDEO_H)
#define OBJ_WORDS    0x400          /* tabla de objetos: 0x800 bytes, 256 entradas de 4 palabras */
/* escenarios, por el código de personaje del oponente (RAM 0xFF8668) */
enum { STAGE_RYU, STAGE_HONDA, STAGE_BLANKA, STAGE_GUILE, STAGE_KEN, STAGE_CHUNLI, STAGE_ZANGIEF, STAGE_DHALSIM, STAGE_BISON, STAGE_SAGAT, STAGE_BALROG, STAGE_VEGA };
#define VIDEO_BACKDROP_PEN 0xbff    /* el fondo es el último pen de la paleta (bitmap.fill(0xbff)) */

/* lo que el cuadro necesita de la tabla de objetos y de los registros de scroll (HFR, docs/HFR.md); no es parte del estado guardado */
#define FZ_WRAM_LO  0x8000          /* 0xFF8000-0xFF8FFF: variables de la partida */
#define FZ_WRAM_LEN 0x1000

typedef struct VideoSnap {
    uint16_t obj[OBJ_WORDS];
    uint16_t scrollx[3], scrolly[3];   /* SCROLL1-3 */
    uint16_t rowoffs;                  /* desplazamiento de la tabla de rowscroll */
    uint16_t rows[0x400];              /* tabla de rowscroll (palabra por línea) */
} VideoSnap;

typedef struct Video {
    const Gfx *gfx;
    uint16_t obj[OBJ_WORDS];        /* tabla de objetos latcheada al empezar el VBlank (m_buffered_obj) */
    uint32_t rgb[PALETTE_ENTRIES];  /* paleta convertida a 0x00RRGGBB */
    uint16_t pix[VIDEO_PIXELS_MAX]; /* pen por píxel (como bitmap_ind16), rw píxeles por fila */
    uint8_t pri[VIDEO_PIXELS_MAX];  /* bitmap de prioridad: bit 0 = tile por delante de los sprites, 31 = sprite */
    uint32_t frame[VIDEO_PIXELS_MAX];   /* cuadro final 0x00RRGGBB, fila a fila, w píxeles por fila */
    /* pantalla ancha: w es el ancho del cuadro que se entrega (VIDEO_W = apagado); rw y rx0 son la geometría del último
     * cuadro renderizado (ancho y coordenada cruda X de su columna 0): VIDEO_W y 64 en las escenas que no se ensanchan,
     * que se centran en el cuadro con barras negras */
    int w, rw, rx0;
    int xsigned;                    /* stage completo (D59): la X de los sprites de la tabla es un entero con signo, sin envolver en 9 bits */
    int hud_edges;                  /* 1: el HUD de combate se pega a los bordes de la ventana ancha (video_hud_side) */
    VideoSnap prev, cur, mid;       /* mid: la instantánea interpolada que dibuja video_render_t */
    int in_vblank;                  /* video_render lo llama el VBlank (avanza prev) o el anfitrión (refresco: prev = cur, sin interpolar) */            /* instantáneas del cuadro anterior y del actual (cada VBlank, prev = cur antes de renderizar) */
    const VideoSnap *st;            /* la instantánea con que se dibuja el cuadro en curso */
    Mem fz;                         /* la máquina congelada al renderizar el cuadro actual: de aquí lee el dibujado (RAM gráfica, paleta, registros) */
    uint8_t fz_gfxram[GFXRAM_SIZE];
    uint8_t fz_wram[WRAM_SIZE];     /* solo vale FZ_WRAM_LO..+FZ_WRAM_LEN: el estado de la partida (modo, fase, oponente) que mira video_scene_wide */
    uint64_t renders, latches;
    int flip_warned;
    /* información del último cuadro, para el visor y los informes */
    int order[4];                   /* capas en orden de dibujo */
    int enabled[4];                 /* [0] sprites (siempre), [1..3] capas de scroll */
    int sprites;                    /* entradas de la tabla de objetos dibujadas */
    int rowscroll;
    uint32_t dk_key[4096];          /* memoria de tile_dark (video.c): clave y resultado, se vacía en cada cuadro */
    uint8_t dk_val[4096];
    int stage_id;                   /* escenario del cuadro: el personaje del oponente (0xFF8668), ver STAGE_* */
    int hide_far;                   /* no dibujar SCROLL3 (elefantes de primer plano de Dhalsim en stage completo) */
    int layer_mask;                 /* depuración (SF2_VIDEO_LAYERS): capas que se dibujan, 0xf = todas */
    int marked, blocked;            /* píxeles marcados por la máscara de prioridad y píxeles de sprite tapados */
    int rgb_fixed;                  /* 1: usar v->rgb tal cual en vez de convertir mem->palette (tools/render_dump.c) */
} Video;

void video_init(Video *v, const Gfx *gfx);
/* ancho del cuadro entregado: VIDEO_W (384, como la recreativa) hasta VIDEO_W_MAX (512), par; 0 si es válido, -1 si no.
 * Con más de 384 el combate se dibuja ensanchado y el resto de pantallas centradas (video_scene_wide).  Surte efecto en el
 * siguiente video_render. */
int video_set_width(Video *v, int w);
/* HUD en los bordes (D55): con la vista ancha, 1 pega las mitades del marcador a los bordes en vez de dejarlas donde las
 * compuso el juego (en el centro de 384 píxeles); el KO y el tiempo se quedan en el centro.  Surte efecto en el siguiente
 * video_render. */
void video_set_hud_edges(Video *v, int on);
/* stage completo (D59): la X de los sprites de la tabla de objetos se interpreta con signo (la escribe así el juego parcheado) */
void video_set_xsigned(Video *v, int on);
/* 1 si la pantalla actual del juego se ensancha: combate (presentación de ronda, pelea, victoria y recuento) con
 * oponente; las demás (título, mapa, selección, VS, diálogo, bonus, final) se componen para 384 píxeles */
int video_scene_wide(const Mem *mem);
/* al empezar cada VBlank: renderiza y después latchea la tabla de objetos (orden de MAME) */
void video_vblank(Video *v, const Mem *mem);
void video_render(Video *v, const Mem *mem);
/* renderiza con una instantánea dada (la actual, v->cur, da el mismo cuadro que video_render; el HFR pasará una interpolada) */
void video_render_snap(Video *v, const VideoSnap *st);
/* HFR (fase 1): dibuja el cuadro en el instante t256 (0 a 256, en 256avos) entre el cuadro anterior y el actual.  Se interpolan la X/Y de las tres
 * capas, el rowscroll y la posición de los sprites que conservan ranura, código y atributos (saltos de más de 64 px en las capas y de 32 en los sprites
 * no se interpolan: son cortes de escena o ranuras reasignadas); todo lo demás (tiles, paleta, animación) es el del cuadro actual.  t256 >= 256 es
 * exactamente video_render_snap(v, &v->cur). */
void video_render_t(Video *v, int t256);
void video_latch_obj(Video *v, const Mem *mem);
struct StateStream;
/* estado guardable: la tabla de objetos latcheada (el resto se recalcula; tras cargar, video_render refresca el cuadro) */
void video_state(Video *v, struct StateStream *s);
/* codifica el último cuadro como PNG RGB de 8 bits (runtime/png.h); el anfitrión lo escribe donde quiera */
int video_encode_png(const Video *v, uint8_t **png, size_t *len);

#endif
