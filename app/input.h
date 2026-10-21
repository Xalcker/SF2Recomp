/*
 * input.h - teclado y mandos (SDL3) -> las tres palabras de entrada de la placa CPS1.
 *
 * Las asignaciones salen de la configuración (platform/config.h): por acción, hasta INPUT_MAX_BINDS teclas
 * (nombres de SDL) y hasta INPUT_MAX_BINDS botones de mando.  El mando 1 maneja al jugador 1 y el 2 al
 * jugador 2 (en el orden en que se conectan); la cruceta y el stick izquierdo mueven siempre.  Las monedas y
 * start de ambos jugadores se leen del teclado y del mando de su jugador.
 */
#ifndef APP_INPUT_H
#define APP_INPUT_H

#include <SDL3/SDL.h>
#include <stdint.h>
#include "config.h"

#define INPUT_MAX_BINDS 4
#define INPUT_MAX_PADS  2

typedef struct Input Input;

/* resuelve los nombres de la configuración; escribe un aviso por stderr por cada nombre desconocido */
Input *input_create(const Sf2Config *cfg);
void input_destroy(Input *in);
/* comprueba que todos los nombres de teclas y botones de la configuración existen en SDL; devuelve cuántos no
 * (descritos en `msg`) */
int input_check_config(const Sf2Config *cfg, char *msg, size_t msgn);

/* vuelve a resolver las asignaciones (tras cambiarlas en el menú); los mandos abiertos no se tocan */
void input_reload(Input *in, const Sf2Config *cfg);

/* sucesos de mandos conectados y desconectados (llamar con cada evento) */
void input_handle_event(Input *in, const SDL_Event *ev);
/* abre los mandos ya conectados al arrancar */
void input_open_existing(Input *in);

/* lee el estado actual: bits SF2_IN0_*, SF2_IN1_* y SF2_IN2_* (1 = pulsado) */
void input_poll(Input *in, uint16_t *in0, uint16_t *in1, uint16_t *in2);

/* ¿coincide `sc` con alguna tecla de la tecla rápida? */
int input_hotkey_matches(const Input *in, int hotkey, SDL_Scancode sc);
/* ¿está pulsada ahora alguna tecla de la tecla rápida? (para las que se mantienen, como el avance rápido) */
int input_hotkey_down(const Input *in, int hotkey);
/* número de mandos abiertos */
int input_pad_count(const Input *in);

#endif
