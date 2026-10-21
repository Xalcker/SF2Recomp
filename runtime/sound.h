/*
 * sound.h - placa de sonido de la CPS1 (hito 4): Z80 a 3,579545 MHz con su ROM de 64 KiB (sf2_09.bin), 2 KiB
 * de RAM, YM2151 (ymfm), OKI MSM6295 con 256 KiB de muestras (sf2_18.bin + sf2_19.bin) y los dos latches que
 * escribe el 68000, más la mezcla a 48 kHz del altavoz mono.
 *
 * Mapa del Z80 (sub_map de cps1.cpp en MAME):
 *   0x0000-0x7FFF ROM          0x8000-0xBFFF banco de ROM (2 x 16 KiB, sf2_09.bin 0x8000-0xFFFF)
 *   0xD000-0xD7FF RAM          0xF000-0xF001 YM2151 (dirección/datos, estado)
 *   0xF002 OKI (comando/estado) 0xF004 banco (bit 0)   0xF006 pin 7 del OKI (bit 0)
 *   0xF008 latch 1 (comando del 68000 en 0x800181)    0xF00A latch 2 (0x800189, "fade")
 *   resto sin mapear: lee 0 y descarta las escrituras.
 *
 * Tiempo: un solo reloj, el ciclo T del Z80 desde el arranque (el YM2151 usa el mismo cristal).  El 68000
 * (10 MHz) y el Z80 arrancan a la vez; el ciclo c del 68000 es el ciclo floor(c * 3579545 / 10^7) del Z80.
 * Como el Z80 no influye en el 68000 en la CPS1, la placa corre "detrás": el 68000 deja sus escrituras en
 * los latches con su ciclo (sound_latch_write) y sound_run_until hace avanzar el Z80 hasta un ciclo dado.
 * El latch cambia para los accesos del Z80 desde el ciclo de la escritura, como con el synchronize() del
 * generic_latch_8 de MAME.
 *
 * Sucesos, en orden de tiempo y antes de cada límite de instrucción y de cada acceso a 0xF000-0xF00F:
 * escrituras de los latches, vencimientos de los temporizadores del YM2151 y sus efectos aplazados
 * (ym2151.h), y la actualización periódica de sonido de MAME (50 Hz, k / 50 s), que es donde se mezcla.
 *
 * Flujos y mezcla (lo que hace sound.cpp de MAME 0.289 con su remuestreador por defecto, "lofi"):
 *   - YM2151 a 55.930 Hz (3579545 / 64) y OKI a 7.575 Hz (1 MHz / 132): antes de cada acceso al chip se
 *     generan sus muestras hasta la del instante del acceso inclusive, floor(t * frecuencia);
 *   - en cada actualización periódica el altavoz (48 kHz) toma las muestras nuevas, desde la siguiente a la
 *     última hasta floor(t * 48000), remuestreando cada ruta (YM L 0,35, YM R 0,35, OKI 0,30) con la
 *     interpolación de 4 puntos de MAME (con media de 2 muestras para el YM, que baja de frecuencia).
 *   El resultado es la mezcla que MAME graba con -wavwrite (float; el WAV es int(x * 32768) saturado).
 */
#ifndef RUNTIME_SOUND_H
#define RUNTIME_SOUND_H

#include <stdint.h>
#include <stdio.h>
#include "z80.h"
#include "ym2151.h"
#include "oki6295.h"
#include "romset.h"

#define SOUND_Z80_HZ      3579545u
#define SOUND_M68K_HZ     10000000u
#define SOUND_YM_RATE     55930u      /* 3579545 / 64, entero como en ymfm */
#define SOUND_OKI_RATE    7575u       /* 1000000 / 132, entero como en okim6295.cpp */
#define SOUND_OUT_RATE    48000u      /* frecuencia de la máquina en MAME (-samplerate) */
#define SOUND_UPDATE_HZ   50u         /* STREAMS_UPDATE_FREQUENCY */

#define SOUND_RING        16384u      /* historia de muestras de cada flujo (potencia de 2) */
#define SOUND_LATCH_QUEUE 256u

typedef struct SoundLatchWrite { uint64_t t; uint8_t which, val; } SoundLatchWrite;

typedef struct SoundLofi {           /* audio_resampler_lofi de MAME */
    uint32_t fs, ft, divide, step;
} SoundLofi;

typedef struct Cps1Sound {
    Z80 z;
    uint8_t rom[0x18000];            /* región "audiocpu" de MAME: 0x0000-0x7FFF y bancos en 0x10000-0x17FFF */
    uint8_t ram[0x800];
    uint8_t okirom[OKI6295_ROM_SIZE];
    int bank;
    uint8_t latch[2];
    Ym2151 *ym;
    Oki6295 oki;

    SoundLatchWrite lq[SOUND_LATCH_QUEUE];
    unsigned lq_head, lq_count;

    /* flujos: número de muestras generadas y anillos con los valores en float (escala de MAME) */
    uint64_t ym_count, oki_count, out_count;
    float ym_l[SOUND_RING], ym_r[SOUND_RING], oki_buf[SOUND_RING];
    uint64_t updates;                /* actualizaciones periódicas hechas (la siguiente es la k = updates + 1) */
    SoundLofi rs_ym, rs_oki;
    float gain_ym, gain_oki;

    /* salidas opcionales */
    FILE *f_events, *f_ym, *f_oki, *f_mono, *f_chunks;   /* los abre y cierra el anfitrión (validación) */
    FILE *f_trace;                   /* traza de pasos del Z80 (sound_set_trace) */
    uint64_t trace_from, trace_to;
    void (*on_audio)(void *user, const float *mono, unsigned n);   /* bloque de 48 kHz recién mezclado */
    void *user;
    uint64_t chunk_ym0, chunk_oki0;  /* cuentas en la actualización anterior (para chunks.txt) */

    /* estadísticas */
    uint64_t latch_writes, latch_reads, ym_writes, ym_reads, oki_writes, oki_reads, unmapped;
    uint64_t pin7_writes, bank_writes;
    int warned_pin7;
} Cps1Sound;

/* toma las ROMs de sonido del RomSet (sf2_9, sf2_18, sf2_19) y deja la placa en reset; 0 si va bien */
int sound_init(Cps1Sound *s, const RomSet *rs);
/* traza cada paso del Z80 entre los ciclos `from` y `to` en `f` (depuración); f = NULL la apaga */
void sound_set_trace(Cps1Sound *s, FILE *f, uint64_t from, uint64_t to);
void sound_free(Cps1Sound *s);

/* apaga y enciende la placa (conserva las ROMs y los ganchos/archivos de salida) */
void sound_reset(Cps1Sound *s);
struct StateStream;
/* estado guardable: Z80, RAM, banco, latches, YM2151, OKI, cola de latches, cuentas y anillos de muestras */
void sound_state(Cps1Sound *s, struct StateStream *st);

/* el 68000 escribió `val` en el latch `which` (1 = 0x800181, 2 = 0x800189) en su ciclo `cyc68` */
void sound_latch_write(Cps1Sound *s, int which, uint8_t val, uint64_t cyc68);
/* hace correr la placa hasta el ciclo del 68000 `cyc68` (el Z80 puede pasarse una instrucción) */
void sound_run_until(Cps1Sound *s, uint64_t cyc68);

/* ciclo del Z80 que corresponde a un ciclo del 68000 */
static inline uint64_t sound_z80_cycle(uint64_t cyc68) {
    return cyc68 * SOUND_Z80_HZ / SOUND_M68K_HZ;
}

/* cierra los archivos de salida e imprime un resumen */
void sound_report(const Cps1Sound *s, FILE *f);

#endif
