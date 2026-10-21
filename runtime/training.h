/*
 * training.h - mod de entrenamiento (docs/ENTRENAMIENTO.md): sin límite de tiempo, barras que se recuperan, jugador 2 como
 * «dummy» manejado por un guion (sin los controles del anfitrión), partida versus automática y registro de las entradas de los
 * dos jugadores para el visor de entradas del anfitrión.
 *
 * Es un mod conmutable: con `enabled = 0` y `track_inputs = 0` no toca nada y la partida es idéntica a MAME.  Funciona sobre la
 * RAM de trabajo y sobre las entradas manuales del 68000 en el VBlank, justo antes de entregar la IRQ2 (el mismo punto donde
 * el juego las lee), así que es determinista: con las mismas entradas del anfitrión y el mismo guion da la misma partida.
 * No es parte del estado guardado.
 *
 * El núcleo no dibuja nada: entrega datos (historial de entradas, cajas) y el anfitrión los pinta con la resolución de su ventana.
 */
#ifndef RUNTIME_TRAINING_H
#define RUNTIME_TRAINING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* entrada canónica de un jugador (la misma para J1 y J2): 1 = pulsado */
#define SF2_PAD_RIGHT 0x001
#define SF2_PAD_LEFT  0x002
#define SF2_PAD_DOWN  0x004
#define SF2_PAD_UP    0x008
#define SF2_PAD_LP    0x010
#define SF2_PAD_MP    0x020
#define SF2_PAD_HP    0x040
#define SF2_PAD_LK    0x080
#define SF2_PAD_MK    0x100
#define SF2_PAD_HK    0x200

typedef enum Sf2DummyMode {
    SF2_DUMMY_STAND,          /* quieto de pie */
    SF2_DUMMY_CROUCH,         /* agachado */
    SF2_DUMMY_JUMP,           /* salta sin parar (vertical) */
    SF2_DUMMY_BLOCK_STAND,    /* se cubre de pie cuando J1 ataca */
    SF2_DUMMY_BLOCK_CROUCH,   /* se cubre agachado cuando J1 ataca */
    SF2_DUMMY_WALK_FWD,       /* camina hacia J1 */
    SF2_DUMMY_WALK_BACK,      /* camina alejándose de J1 */
    SF2_DUMMY_SCRIPT,         /* sigue `script` en bucle */
    SF2_DUMMY_COUNT
} Sf2DummyMode;

#define SF2_TRAIN_SCRIPT_LEN 256

typedef struct Sf2Training {
    int enabled;              /* interruptor general del mod (apagado: juego original) */
    int infinite_time;        /* el temporizador del asalto se queda en 99 */
    int refill;               /* 0 no, 1 la barra se llena tras `refill_delay` cuadros sin recibir daño, 2 siempre llena (sin KO) */
    int refill_delay;         /* cuadros (1-600) */
    int lock_p2;              /* el anfitrión no maneja a J2: lo manejan el modo del dummy o el guion */
    int auto_vs;              /* en el título mete monedas y pulsa start de J1 y J2 a la vez (versus), y J2 elige luchador solo */
    int dummy;                /* Sf2DummyMode */
    char script[SF2_TRAIN_SCRIPT_LEN];   /* guion del dummy: ver sf2_training_set */
    int track_inputs;         /* registra el historial de entradas de J1 y J2 (para el visor de entradas) */
} Sf2Training;

#define SF2_TRAIN_HISTORY 24     /* eventos de entrada que se conservan por jugador */

typedef struct Sf2InputEvent {
    uint16_t mask;            /* SF2_PAD_* que estaban pulsados */
    uint32_t frames;          /* cuadros que se mantuvo (el más reciente, los que lleva hasta ahora; satura en 999) */
} Sf2InputEvent;

/* cajas de golpe y de daño (docs/HITBOXES.md): coordenadas de la pantalla de 384x224 del juego, esquina superior izquierda y tamaño */
typedef enum Sf2BoxKind {
    SF2_BOX_HEAD, SF2_BOX_BODY, SF2_BOX_FOOT, SF2_BOX_WEAK,      /* de daño (hurtboxes) */
    SF2_BOX_ATTACK,                                              /* de golpe */
    SF2_BOX_PUSH,                                                /* de empuje («atari») */
    SF2_BOX_PROJ_ATTACK, SF2_BOX_PROJ_PUSH                       /* lo mismo para un proyectil */
} Sf2BoxKind;

typedef struct Sf2Box {
    int16_t x, y, w, h;
    uint8_t kind;             /* Sf2BoxKind */
    uint8_t owner;            /* 0 = J1, 1 = J2, 2 = proyectil */
} Sf2Box;

#define SF2_MAX_BOXES 64

#ifdef __cplusplus
}
#endif

#endif
