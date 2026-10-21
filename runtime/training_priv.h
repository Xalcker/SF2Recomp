/* training_priv.h - estado interno del mod de entrenamiento (lo embebe sf2.c; el resto del proyecto usa training.h). */
#ifndef RUNTIME_TRAINING_PRIV_H
#define RUNTIME_TRAINING_PRIV_H

#include <stdio.h>
#include "mem.h"
#include "training.h"

#define TRAINING_MAX_STEPS 64

typedef struct Training {
    Sf2Training cfg;
    struct { uint16_t bits, frames; } steps[TRAINING_MAX_STEPS];     /* guion del dummy (bits canónicos, con direcciones relativas) */
    int nsteps, step, step_left;
    uint64_t tick;                       /* VBlanks desde el último reinicio del mod */
    uint16_t dummy_bits;                 /* entrada canónica de J2 para el próximo cuadro */
    uint16_t pulse;                      /* bits de IN0 que se suman (monedas y start de la partida versus automática) */
    int life_prev[2], life_stable[2];    /* recuperación de barras */
    int sel_ticks, p1_busy, auto_wait, p2_right;
    uint8_t last_phase;
    Sf2InputEvent hist[2][SF2_TRAIN_HISTORY];
    int hist_n[2];
    uint16_t now[2];
} Training;

void training_init(Training *t);
/* aplica una configuración nueva; si el guion no vale devuelve -1 con el motivo y no cambia nada */
int training_configure(Training *t, const Sf2Training *cfg, char *err, size_t errn);
void training_reset(Training *t);
/* entradas finales a partir de las del anfitrión (las del dummy y las monedas ya calculadas) */
void training_inputs(const Training *t, uint16_t h0, uint16_t h1, uint16_t h2, uint16_t *o0, uint16_t *o1, uint16_t *o2);
/* en cada VBlank, antes de entregar la IRQ2: toca la RAM, calcula las entradas del cuadro siguiente en out[0..2] */
void training_vblank(Training *t, Mem *m, const uint16_t host[3], uint16_t out[3]);
int training_history(const Training *t, int player, Sf2InputEvent *out, int max);
uint16_t training_canon(int player, uint16_t in1, uint16_t in2);
/* cajas del cuadro actual (vacío fuera del combate) */
int training_boxes(const Mem *m, Sf2Box *out, int max);

#endif
