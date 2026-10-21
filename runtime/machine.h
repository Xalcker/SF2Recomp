/*
 * machine.h - la máquina CPS1 sobre el código recompilado: CPU, memoria, planificador de la IRQ2 por ciclos,
 * placa de sonido opcional y ganchos por cuadro.  Es el núcleo común de todos los ejecutables (juego SDL3,
 * navegador, arneses de validación); no conoce archivos, ventanas ni el sistema operativo.
 *
 * Planificador: la IRQ2 (VBlank) se marca pendiente cada `frame_cycles` ciclos estimados (167.680 por
 * defecto, el cuadro de 59,637 Hz de MAME) y se entrega en el siguiente límite de instrucción del código
 * generado (gancho rt_hook), el único punto seguro: los registros y la memoria están en un estado
 * coherente y el manejador recompilado se ejecuta anidado hasta su rte.  Un cuadro = una entrega de IRQ2.
 *
 * Ganchos (hito 3): `on_vblank` se llama al cruzar el umbral de ciclos (el comienzo del VBlank), antes de
 * marcar la IRQ2 pendiente: ahí el vídeo renderiza el cuadro y latchea la tabla de objetos, en el mismo
 * orden que vblank_begin de MAME (cuadro, copia de sprites, IRQ).  `on_sample` se llama en el punto de
 * muestra 0xAD0 del manejador (el instante en que tools/mame_blind.lua escribe el resumen y guarda la
 * captura), con el número de cuadro.
 *
 * Sonido (hito 4): si `sound` apunta a una placa (runtime/sound.h), las escrituras del 68000 en los latches le
 * llegan con su ciclo y la placa corre hasta el ciclo del 68000 en cada VBlank, antes de `on_vblank`.
 *
 * Ejecución (hito 5): `machine_run` corre hasta el final de la partida (arneses).  `machine_step` corre solo
 * hasta el siguiente punto de cesión y vuelve, de modo que el anfitrión (bucle de SDL, requestAnimationFrame
 * del navegador) conserva el control entre cuadros.  El código recompilado anida llamadas C, así que la cesión
 * descarta esa pila con longjmp y reanuda por la tabla de despacho, que es el mismo mecanismo con el que el
 * código ya cambia de tarea (D9).  Por eso solo se cede en una dirección de la tabla (una instrucción
 * cualquiera no tiene etiqueta en su función), fuera de un manejador de IRQ, y después de haber pasado un
 * VBlank.  El estado de la máquina no cambia con la cesión: la partida es idéntica con cualquier
 * granularidad (comprobado en tools/ con el resumen de 12.000 cuadros).
 *
 * Sin errores no recuperables en el núcleo: rt_fatal deja el mensaje en `error` y devuelve el control a
 * quien llamó a machine_run / machine_step con MACHINE_ERROR.
 */
#ifndef RUNTIME_MACHINE_H
#define RUNTIME_MACHINE_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "sf2_port.h"
#include "cpu.h"
#include "mem.h"
#include "cps1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Machine Machine;
struct Cps1Sound;
struct StateStream;

enum { MACHINE_RUNNING = 0, MACHINE_YIELD = 1, MACHINE_DONE = 2, MACHINE_ERROR = 3 };

struct Machine {
    M68K cpu;                   /* primer miembro: los ganchos rt_* reciben M68K * y recuperan la máquina de él */
    Mem mem;
    Cps1 cps;
    uint64_t next_irq;
    uint64_t irq_time;          /* ciclo en que se afirmó la última IRQ2 (para el punto de muestreo, D47) */
    /* punto de muestreo de interrupciones (D47): comienzo de la instrucción anterior, fin del último acceso al bus y
     * ciclo hasta el que una IRQ afirmada se atiende tras la instrucción anterior */
    uint64_t ins_start, acc_end, ack;
    uint64_t max_frames, max_insns;     /* 0 = sin límite; al pasarlos la máquina termina con MACHINE_DONE */
    uint64_t irqs_taken, vblanks;
    int quiet;
    void (*on_vblank)(Machine *mc);
    void (*on_sample)(Machine *mc, uint64_t frame);
    void (*on_insn)(Machine *mc, uint32_t pc);      /* antes de cada instrucción (cobertura, anillo de traza) */
    int (*on_unknown)(Machine *mc);                 /* PC sin función recompilada: devuelve 1 si lo resolvió (Musashi) */
    /* cada despacho por la tabla (salto indirecto, retorno no estructurado, excepción, trampolín): instrucción de
     * origen (la última ejecutada), destino y si estaba en la tabla.  Cobertura del hito 6; NULL en el juego. */
    void (*on_dispatch)(Machine *mc, uint32_t from, uint32_t to, int known);
    void *user;
    struct Cps1Sound *sound;    /* placa de sonido opcional (machine_attach_sound) */
    FILE *cmd_log;              /* lista de comandos de sonido por cuadro (machine_log_sound_cmds) */
    uint64_t sample_frame;      /* cuadros cuyo punto de muestra ya pasó (la numeración de tools/mame_*.lua) */

    /* control de ejecución */
    int status;                 /* MACHINE_RUNNING mientras se puede seguir */
    int started;                /* la CPU ya salió del reset */
    int yield_enabled;          /* machine_step: ceder tras cada VBlank; 0 en machine_run */
    int yield_pending;          /* ha pasado un VBlank y falta una dirección de la tabla donde ceder */
    int skip_hook;              /* al reanudar tras ceder, el gancho de la instrucción en curso ya se ejecutó */
    int in_irq;                 /* dentro del manejador de IRQ (no se cede) */
    int in_fallback;            /* dentro de on_unknown */
    jmp_buf resume;
    uint32_t last_pc;           /* última instrucción ejecutada (origen de un despacho; solo informativo) */
    uint64_t dispatch_misses;   /* destinos que no estaban en la tabla de despacho */
    uint32_t miss_from, miss_to;        /* el último de ellos: instrucción de origen y dirección de destino */
    char error[384];
};

SF2_STATIC_ASSERT(offsetof(Machine, cpu) == 0, "los ganchos rt_* suponen M68K como primer miembro de Machine");

/* prepara memoria y máquina mínima; `rom` es la imagen de 1 MiB (no se copia, debe vivir lo que la máquina).
 * Deja la CPU lista para el primer machine_run / machine_step (equivale a encender la placa). */
void machine_init(Machine *mc, const uint8_t *rom, uint64_t frame_cycles);
/* libera lo que reservó machine_init */
void machine_free(Machine *mc);
/* apaga y enciende: RAM a cero, CPU en reset, reloj a cero, placa de sonido en reset si está conectada */
void machine_reset(Machine *mc);
/* conecta la placa de sonido: latches del 68000 y avance por VBlank */
void machine_attach_sound(Machine *mc, struct Cps1Sound *snd);
/* anota en `f` cada escritura del 68000 en los latches de sonido, una por línea: "cuadro latch valor" (el
 * cuadro es el de la numeración de tools/mame_sound_cmds.lua, el último punto de muestra pasado; el valor en
 * hexadecimal de dos cifras).  No necesita la placa de sonido ni sus ROMs. */
void machine_log_sound_cmds(Machine *mc, FILE *f);

/* corre sin ceder hasta max_frames / max_insns (MACHINE_DONE) o hasta un error (MACHINE_ERROR) */
int machine_run(Machine *mc);
/* corre hasta el siguiente punto de cesión tras un VBlank (MACHINE_YIELD), o hasta el final / un error */
int machine_step(Machine *mc);

/* comprueba el reloj y entrega la IRQ2 si toca; para quien ejecuta código ajeno (la red de seguridad de
 * Musashi en los arneses) y debe dejar pasar los cuadros */
void machine_poll_irq(Machine *mc);

/* estado guardable de la máquina completa (CPU, memoria, cuadro, placa de sonido si hay) */
void machine_state(Machine *mc, struct StateStream *s);

#ifdef __cplusplus
}
#endif

#endif
