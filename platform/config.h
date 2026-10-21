/*
 * config.h - configuración del juego: dipswitches, escala de la ventana, volumen y remapeo de teclado y mando.
 *
 * Archivo de texto tipo INI (secciones entre corchetes, "clave = valor", comentarios con # o ;).  Este módulo
 * no sabe de SDL: guarda los nombres de teclas y botones como texto (los nombres de SDL: "Up", "Z", "Left Shift";
 * "a", "dpup", "rightshoulder", "righttrigger") y la capa SDL los resuelve al arrancar y avisa de los que no
 * existan.  Los valores desconocidos o fuera de rango no abortan: se avisan y se queda el valor por omisión.
 */
#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* acciones de juego, en el orden en que se escriben en el archivo */
typedef enum Sf2Action {
    SF2_ACT_COIN1, SF2_ACT_START1, SF2_ACT_COIN2, SF2_ACT_START2, SF2_ACT_SERVICE,
    SF2_ACT_P1_UP, SF2_ACT_P1_DOWN, SF2_ACT_P1_LEFT, SF2_ACT_P1_RIGHT,
    SF2_ACT_P1_LP, SF2_ACT_P1_MP, SF2_ACT_P1_HP, SF2_ACT_P1_LK, SF2_ACT_P1_MK, SF2_ACT_P1_HK,
    SF2_ACT_P2_UP, SF2_ACT_P2_DOWN, SF2_ACT_P2_LEFT, SF2_ACT_P2_RIGHT,
    SF2_ACT_P2_LP, SF2_ACT_P2_MP, SF2_ACT_P2_HP, SF2_ACT_P2_LK, SF2_ACT_P2_MK, SF2_ACT_P2_HK,
    SF2_ACT_COUNT
} Sf2Action;

/* teclas del anfitrión (no del juego) */
typedef enum Sf2Hotkey {
    SF2_HK_SAVE_STATE, SF2_HK_LOAD_STATE, SF2_HK_SLOT_PREV, SF2_HK_SLOT_NEXT,
    SF2_HK_PAUSE, SF2_HK_FAST_FORWARD, SF2_HK_FULLSCREEN, SF2_HK_SCREENSHOT, SF2_HK_RESET, SF2_HK_QUIT,
    SF2_HK_WIDESCREEN, SF2_HK_WIDE_LOGIC, SF2_HK_STAGE_FULL, SF2_HK_HFR, SF2_HK_MENU,
    SF2_HK_COUNT
} Sf2Hotkey;

#define SF2_BIND_LEN 96          /* texto de las asignaciones de una acción: "Up, W" */

typedef struct Sf2Config {
    /* [video] */
    int scale;                   /* 0 = ajustar a la pantalla; si no, ventana de 384*N x 224*N */
    int fullscreen;
    int integer_scale;           /* escala entera (sin deformar píxeles); solo con aspect = pixel */
    int aspect43;                /* 0 = píxeles cuadrados (384x224), 1 = pantalla 4:3 como el monitor de la recreativa */
    int smooth;                  /* 0 = vecino más cercano, 1 = lineal */
    int vsync;
    int widescreen;              /* pantalla ancha real: más escenario a los lados en combate (sin estirar) */
    int hfr;                     /* cuadros intermedios al refresco del monitor (docs/HFR.md); solo presentación, no cambia el juego */
    int stage_full;              /* con pantalla ancha, cámara fija y todo el escenario a la vista (cambia el juego) */
    int wide_logic;              /* con pantalla ancha, paredes y cámara con el ancho de la ventana ancha (cambia el juego) */
    int hud_edges;               /* con pantalla ancha, el marcador del combate pegado a los bordes (hud = edges) */
    int wide_width;              /* ancho del cuadro en pantalla ancha: par, de 384 a 512 (512 = 16:9 con aspect = 4:3) */
    /* [audio] */
    int audio;                   /* 0 = sin sonido */
    int volume;                  /* 0-100 */
    int audio_rate;              /* frecuencia del dispositivo (la mezcla es de 48 kHz y SDL la convierte) */
    /* [dip]: valores con nombre; dsw() los combina */
    int difficulty;              /* 0 (más fácil) a 7 (más difícil); 3 = normal */
    int coin_a, coin_b;          /* índice de tabla de precios (config_coinage_names) */
    int free_play, two_coins_to_start, demo_sounds, allow_continue, freeze, test_mode;
    /* [paths] */
    char rom_path[512];          /* sf2.zip o carpeta con las ROMs ("" = buscar) */
    char state_dir[512];         /* carpeta de estados guardados ("" = la de preferencias) */
    /* [keys] [pad] [hotkeys] */
    char keys[SF2_ACT_COUNT][SF2_BIND_LEN];
    char pads[SF2_ACT_COUNT][SF2_BIND_LEN];
    char hotkeys[SF2_HK_COUNT][SF2_BIND_LEN];
} Sf2Config;

void config_defaults(Sf2Config *c);
/* interpreta el texto de un archivo de configuración sobre `c`; devuelve el número de avisos, que se
 * escriben (uno por línea) en `warn` */
int config_parse(Sf2Config *c, const char *text, char *warn, size_t warn_size);
/* escribe la configuración completa, con comentarios; devuelve la longitud (aunque no quepa) */
size_t config_format(const Sf2Config *c, char *out, size_t cap);

/* dipswitches resultantes (A, B, C) */
void config_dsw(const Sf2Config *c, uint8_t *a, uint8_t *b, uint8_t *dswc);

const char *config_action_name(int action);        /* "p1_up", "coin1"... */
const char *config_hotkey_name(int hotkey);
extern const char *const config_coinage_names[8];  /* "4c1c" ... "1c6c" */

#ifdef __cplusplus
}
#endif

#endif
