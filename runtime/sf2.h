/*
 * sf2.h - fachada del núcleo para los ejecutables del juego (SDL3, navegador, medidas de rendimiento):
 * una máquina completa (68000 recompilado + CPS1 + vídeo + sonido) que se maneja con entradas, "corre un
 * cuadro" y entrega imagen, audio y estados guardados.  No abre archivos ni ventanas: recibe las ROMs ya
 * cargadas (RomSet) y devuelve memoria.
 *
 *   Sf2 *s;  char err[512];
 *   if (sf2_create(&s, &roms, &opt, err, sizeof err) < 0) ...          // verifica las ROMs contra el código recompilado
 *   for (;;) {
 *       sf2_set_input(s, in0, in1, in2);                                  // bits 1 = pulsado
 *       if (sf2_run_frame(s) < 0) ...                                     // sf2_error(s)
 *       present(sf2_frame(s)); queue_audio(sf2_audio(s, &n), n); sf2_audio_clear(s);
 *   }
 */
#ifndef RUNTIME_SF2_H
#define RUNTIME_SF2_H

#include <stddef.h>
#include <stdint.h>
#include "romset.h"
#include "training.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SF2_SCREEN_W   384
#define SF2_SCREEN_H   224
#define SF2_SCREEN_W_MAX 800                  /* techo de la pantalla ancha (16:9 con la forma de píxel de la recreativa) */
#define SF2_AUDIO_RATE 48000u                 /* frecuencia de la mezcla validada contra MAME (D31) */
#define SF2_FRAME_HZ   (10000000.0 / 167680.0)   /* 59,637 Hz */

/* bits de las entradas (1 = pulsado) para sf2_set_input; los mismos nombres que tools/inputs_sf2.txt */
#define SF2_IN0_COIN1   0x0001
#define SF2_IN0_COIN2   0x0002
#define SF2_IN0_SERVICE 0x0004
#define SF2_IN0_START1  0x0010
#define SF2_IN0_START2  0x0020
#define SF2_IN0_TEST    0x0040
/* in1: P1 en los bits 0-7, P2 en los 8-15: derecha, izquierda, abajo, arriba, puñetazo débil/medio/fuerte */
#define SF2_IN1_RIGHT   0x0001
#define SF2_IN1_LEFT    0x0002
#define SF2_IN1_DOWN    0x0004
#define SF2_IN1_UP      0x0008
#define SF2_IN1_LP      0x0010
#define SF2_IN1_MP      0x0020
#define SF2_IN1_HP      0x0040
/* in2: patadas débil/media/fuerte de P1 (bits 0-2) y P2 (bits 4-6) */
#define SF2_IN2_LK      0x0001
#define SF2_IN2_MK      0x0002
#define SF2_IN2_HK      0x0004

typedef struct Sf2Options {
    int video;                 /* 0 = no renderizar (solo medidas) */
    int sound;                 /* 0 = sin placa de sonido (solo medidas) */
    uint8_t dswa, dswb, dswc;  /* dipswitches (ver mem.c); 0xFF/0xFC/0x9F son los de MAME */
} Sf2Options;

typedef struct Sf2 Sf2;

/* opciones por omisión: vídeo y sonido activados, dipswitches de MAME */
void sf2_options_default(Sf2Options *o);

/* construye la máquina a partir de las ROMs (que el llamador puede liberar después).  Devuelve 0 o -1 con un
 * mensaje legible en `err`: ROMs incompletas, o que no corresponden al código recompilado (CRC del programa). */
int sf2_create(Sf2 **out, const RomSet *roms, const Sf2Options *opt, char *err, size_t errn);
void sf2_destroy(Sf2 *s);
/* apaga y enciende la placa (las entradas y los dipswitches se conservan) */
void sf2_reset(Sf2 *s);

void sf2_set_input(Sf2 *s, uint16_t in0, uint16_t in1, uint16_t in2);
/* secuencia de entradas por cuadros (texto "desde hasta tecla", ver tools/inputs_sf2.txt) que se suma a las de
 * sf2_set_input: sirve para pruebas y medidas reproducibles.  0 si va bien, -1 con el motivo en `err`. */
int sf2_load_input_script(Sf2 *s, const char *text, const char *name, char *err, size_t errn);
void sf2_set_dips(Sf2 *s, uint8_t a, uint8_t b, uint8_t c);
/* Herramientas de desarrollo ocultas de Capcom (docs/HERRAMIENTAS_DEV.md): ya están en el código recompilado pero el juego solo las
 * abre con una combinación de entrada que no existe en la placa.  `tool` 0 = juego normal; 1 = prueba de scroll, 2 = OBJ TEST (cajas
 * de golpe).  Reinicia la máquina como un encendido: mantiene pulsados P2 puñetazo débil y fuerte al arrancar
 * (activa la bandera 0x2D7 junto con el dip de pruebas, que se fuerza mientras esté activa) y el botón de P1 que elige la herramienta
 * hasta que el juego la abre.  CAMBIA LA PARTIDA (con 0 es idéntica a MAME); no forma parte del estado guardado. */
void sf2_set_dev_tool(Sf2 *s, int tool);
int sf2_dev_tool(const Sf2 *s);

/* corre hasta el siguiente VBlank; devuelve cuántos han pasado (normalmente 1) o -1 si el juego falló */
int sf2_run_frame(Sf2 *s);
const char *sf2_error(const Sf2 *s);
/* 1 si el juego se detuvo porque saltó a una dirección que el recompilador no vio (no está en la tabla de
 * despacho), con la instrucción de origen y el destino; 0 si el fallo fue otro o no lo hubo.  El remedio es
 * añadir "entry <destino>" a tools/hints.txt y regenerar (sf2_error lo dice en una frase). */
int sf2_dispatch_failure(const Sf2 *s, uint32_t *from, uint32_t *to);
/* solo pruebas: al reanudar, el 68000 salta a `pc` como si el juego hubiera saltado a una dirección que el
 * recompilador no vio, para comprobar el registro de fallos de despacho (el siguiente sf2_run_frame devuelve -1) */
void sf2_debug_jump(Sf2 *s, uint32_t pc);

/* último cuadro: sf2_frame_width() x SF2_SCREEN_H píxeles 0x00RRGGBB (por omisión SF2_SCREEN_W x SF2_SCREEN_H) */
const uint32_t *sf2_frame(const Sf2 *s);
/* Pantalla ancha: ancho del cuadro entregado, par y entre SF2_SCREEN_W (apagada, idéntico a MAME) y SF2_SCREEN_W_MAX.
 * Con más de SF2_SCREEN_W el combate se dibuja con más escenario a los lados y las demás pantallas centradas con
 * barras negras, así que el ancho del cuadro no cambia entre escenas.  No es parte del estado guardado ni cambia la
 * partida.  Devuelve 0 o -1 si el ancho no vale; se refresca el cuadro actual. */
int sf2_set_video_width(Sf2 *s, int width);
int sf2_frame_width(const Sf2 *s);
/* HFR (docs/HFR.md, fase 0): instantánea de vídeo `which` (0 = cuadro anterior, 1 = actual): tabla de objetos (SF2_OBJ_WORDS palabras)
 * y X/Y de SCROLL1-3 (scroll[0..2] = X, scroll[3..5] = Y); cualquiera de los dos punteros puede ser NULL.
 * sf2_render_snapshot vuelve a dibujar el último cuadro desde la instantánea actual (t = 1) sin tocar la partida. */
#define SF2_OBJ_WORDS 0x400
void sf2_video_snapshot(const Sf2 *s, int which, uint16_t *obj, uint16_t *scroll);
void sf2_render_snapshot(Sf2 *s);
/* HFR (fase 1): dibuja el último cuadro del juego en el instante t256 (0 a 256) entre el cuadro anterior y el actual; sf2_frame() devuelve ese cuadro
 * hasta el siguiente sf2_run_frame.  256 es el cuadro normal.  No toca la partida ni el audio. */
void sf2_render_interp(Sf2 *s, int t256);
/* HUD en los bordes (D55): con pantalla ancha, el marcador del combate se pega a los bordes de la ventana (por omisión se queda
 * donde lo compone el juego, en el centro).  No es parte del estado ni cambia la partida. */
void sf2_set_hud_edges(Sf2 *s, int on);
/* Lógica ancha (D58, apagada por omisión = juego original): con pantalla ancha, las paredes de los luchadores y los topes de la
 * cámara usan el ancho de la ventana ancha en vez de 384 píxeles, así que se puede luchar más lejos y la vista no sale del arte.
 * CAMBIA LA PARTIDA (ya no coincide con MAME): no es parte del estado guardado, y un estado guardado en un modo puede cargarse en el
 * otro (los luchadores que queden fuera de las paredes originales se recolocan al siguiente cuadro).  `extra` devuelve los
 * píxeles por lado que se aplican ahora (0 si está apagada o el cuadro mide 384). */
void sf2_set_wide_logic(Sf2 *s, int on);
int sf2_wide_logic_extra(const Sf2 *s);
/* Stage completo (D59, apagado = juego original): con pantalla ancha, la cámara de los combates queda fija en el centro del recorrido y
 * los luchadores recorren todo el escenario, que se ve entero cuando el cuadro mide 768 (el recorrido de las 12 pantallas de lucha).
 * Implica la lógica ancha; cambia la partida (ya no coincide con MAME) y no forma parte del estado guardado. */
void sf2_set_stage_full(Sf2 *s, int on);
/* Mod de entrenamiento (training.h, docs/ENTRENAMIENTO.md; apagado = juego original): sin límite de tiempo, barras que se recuperan, J2 como
 * dummy con guion y versus automático.  `enabled` es el interruptor general; `track_inputs` solo registra las entradas de J1 y J2 y no
 * cambia la partida.  Sintaxis del guion (modo SF2_DUMMY_SCRIPT, en bucle): pasos separados por espacios o comas, `botones:cuadros`, con
 * `+` para pulsar varios a la vez: n (nada), f b u d df db uf ub (relativas al rival; l r absolutas), lp mp hp lk mk hk;
 * por ejemplo "d:30 df:3 f+lp:2 n:40".  Devuelve -1 con el motivo si el guion no vale (y no cambia nada).  CAMBIA LA PARTIDA con
 * `enabled`; no es parte del estado guardado. */
int sf2_training_set(Sf2 *s, const Sf2Training *t, char *err, size_t errn);
void sf2_training_get(const Sf2 *s, Sf2Training *t);
/* historial de entradas del jugador 0/1 (más reciente primero) para el visor del anfitrión: devuelve cuántos eventos escribió (como mucho `max`) */
int sf2_training_history(const Sf2 *s, int player, Sf2InputEvent *out, int max);
/* cajas del cuadro actual (docs/HITBOXES.md), pantalla de 384x224; devuelve cuántas escribió (0 fuera del combate) */
int sf2_training_boxes(const Sf2 *s, Sf2Box *out, int max);
/* entrada canónica (SF2_PAD_*) que recibe el juego ahora mismo, del jugador 0/1 */
uint16_t sf2_training_pad(const Sf2 *s, int player);
/* audio mezclado desde la última llamada a sf2_audio_clear: muestras mono float a SF2_AUDIO_RATE */
const float *sf2_audio(const Sf2 *s, size_t *count);
void sf2_audio_clear(Sf2 *s);

uint64_t sf2_frame_count(const Sf2 *s);       /* cuadros del juego (entregas de IRQ2) */
/* gancho de muestra (pruebas): se llama en el punto de la IRQ2 donde tools/mame_snap.lua toma su captura, con el
 * número de cuadro y la imagen que MAME tendría en ese instante (la numeración de los PNG de make compare-video) */
typedef void (*Sf2SampleHook)(void *user, uint64_t frame, const uint32_t *rgb);
void sf2_set_sample_hook(Sf2 *s, Sf2SampleHook hook, void *user);
uint64_t sf2_vblank_count(const Sf2 *s);
/* el juego pidió el volteo de pantalla (no implementado, D24) */
int sf2_flip_requested(const Sf2 *s);

/* ---- estados guardados ------------------------------------------------------------------------- */
/* Un estado es un bloque de bytes con cabecera (marca, versión, CRC del código recompilado y del contenido).
 * Se guarda y se carga entre cuadros; sirve entre plataformas distintas. */
size_t sf2_state_size(Sf2 *s);                                   /* tamaño exacto que ocupará ahora */
int sf2_state_save(Sf2 *s, uint8_t *buf, size_t cap);            /* bytes escritos, o -1 */
/* CRC-32 del contenido del estado (sin cabecera): la misma partida da el mismo valor en cualquier plataforma */
uint32_t sf2_state_crc(Sf2 *s);
/* carga un estado; si no es válido deja la máquina como estaba y devuelve -1 con el motivo en `err` */
int sf2_state_load(Sf2 *s, const uint8_t *buf, size_t len, char *err, size_t errn);

#ifdef __cplusplus
}
#endif

#endif
