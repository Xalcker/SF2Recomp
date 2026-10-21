/*
 * ym2151.h - YM2151 (OPM) para la placa de sonido CPS1 sobre el núcleo ymfm (third_party/ymfm, BSD-3), con
 * la misma interfaz de tiempo que el dispositivo de MAME (ymfm_mame.h).
 *
 * El tiempo se mide en ciclos del reloj del chip (3,579545 MHz, el mismo reloj que el Z80 de la CPS1, así
 * que un ciclo del chip es un ciclo T del Z80).  ymfm no lleva reloj propio para los temporizadores ni para
 * la bandera de ocupado: los pide a la interfaz, y aquí se resuelven así (como MAME):
 *
 *   - ocupado: cada escritura de datos marca ocupado hasta now + 64 ciclos; la lectura de estado compara con
 *     el instante del acceso;
 *   - temporizadores A y B: cada uno es un suceso de una sola vez en now + duración; al vencer, ymfm pone su
 *     bandera, recalcula la IRQ y lo vuelve a programar;
 *   - escritura del registro de modo (0x14) y recálculo de la IRQ durante un acceso de la CPU: MAME los aplaza
 *     con scheduler().synchronize(), un temporizador que vence en el instante del acceso; corta la porción de
 *     tiempo del Z80 al final del ciclo M en curso, pero dentro del callback machine().time() devuelve el
 *     vencimiento del temporizador (device_scheduler::time), así que los temporizadores del chip se cuentan
 *     desde el acceso.  Aquí se encolan para el instante `sync` que da quien hace el acceso (el del acceso:
 *     solo cambia el orden, se aplican después de él).
 *
 * Las muestras (una cada 64 ciclos, 55.930 Hz, dos canales enteros de 16 bits con signo como las da ymfm)
 * las genera quien usa el chip, hasta el instante de cada acceso, igual que MAME actualiza el flujo antes de
 * leer o escribir (ver runtime/sound.c).
 */
#ifndef RUNTIME_YM2151_H
#define RUNTIME_YM2151_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YM2151_NEVER UINT64_MAX

typedef struct Ym2151 Ym2151;

struct StateStream;
/* estado guardable: el chip (save_restore de ymfm) y la interfaz de tiempo (temporizadores, ocupado, aplazados) */
void ym2151_state(Ym2151 *y, struct StateStream *s);

Ym2151 *ym2151_new(void);
void ym2151_free(Ym2151 *y);
void ym2151_reset(Ym2151 *y);

/* acceso de la CPU en el ciclo `now`; los efectos aplazados se aplican en `sync` (>= now) */
void ym2151_write(Ym2151 *y, uint64_t now, uint64_t sync, unsigned offset, uint8_t data);
uint8_t ym2151_read(Ym2151 *y, uint64_t now, uint64_t sync, unsigned offset);

/* próximo suceso interno (vencimiento de temporizador o escritura aplazada), YM2151_NEVER si no hay */
uint64_t ym2151_next_event(const Ym2151 *y);
/* ejecuta el próximo suceso (el reloj del chip pasa a su instante) */
void ym2151_run_event(Ym2151 *y);

/* nivel de la salida IRQ (activa = 1) */
int ym2151_irq(const Ym2151 *y);
/* genera n muestras estéreo intercaladas (L, R) */
void ym2151_generate(Ym2151 *y, int32_t *lr, unsigned n);

#ifdef __cplusplus
}
#endif

#endif
