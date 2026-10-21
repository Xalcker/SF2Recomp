/*
 * oki6295.h - OKI MSM6295: cuatro voces ADPCM de 4 bits sobre 256 KiB de muestras (sf2_18.bin + sf2_19.bin).
 *
 * Mismo comportamiento que okim6295.cpp / okiadpcm.cpp de MAME, que es la referencia de la comparación:
 *   - comando de dos bytes: 1xxxxxxx elige la frase (tabla de 8 bytes por frase al principio de la ROM con
 *     inicio y fin de 18 bits), el segundo byte lleva las voces a arrancar (bits 4-7) y la atenuación
 *     (bits 0-3); una voz que ya suena no se rearranca; un byte 0xxxxxxx sin comando pendiente para las voces
 *     de los bits 3-6;
 *   - lectura: 0xF0 | voces que suenan;
 *   - ADPCM de OKI: paso inicial 0, señal de 12 bits con saturación, tabla de pasos floor(16 * 1,1^n);
 *   - salida de cada voz: (int)(señal * volumen) / 2048 como float, sumada en orden de voz.
 *
 * Frecuencia de muestreo: reloj / 132 con el pin 7 alto (1 MHz / 132 = 7.575 Hz en la CPS1) y reloj / 165 con
 * el pin 7 bajo.  SF2 deja el pin 7 alto (lo escribe a 1 en 0xF006 al arrancar).
 */
#ifndef RUNTIME_OKI6295_H
#define RUNTIME_OKI6295_H

#include <stdint.h>
#include <stddef.h>

#define OKI6295_ROM_SIZE 0x40000u

typedef struct OkiVoice {
    int playing;
    uint32_t base, sample, count;
    int32_t signal, step;
    float volume;
} OkiVoice;

typedef struct Oki6295 {
    const uint8_t *rom;          /* OKI6295_ROM_SIZE bytes */
    int command;                 /* frase pendiente del primer byte, -1 si no hay */
    int pin7;
    OkiVoice voice[4];
    uint64_t starts, stops, rejected;
} Oki6295;

struct StateStream;
void oki6295_state(Oki6295 *o, struct StateStream *s);   /* comando pendiente, pin 7 y voces (la ROM no se guarda) */
/* tabla de pasos de la señal, floor(16 * 1,1^n), n = 0..48 (expuesta para las pruebas) */
extern const int oki6295_step_table[49];
void oki6295_init(Oki6295 *o, const uint8_t *rom);
void oki6295_reset(Oki6295 *o);
void oki6295_write(Oki6295 *o, uint8_t data);
uint8_t oki6295_read(const Oki6295 *o);
/* una muestra de salida (suma de las voces, escala de MAME: 1,0 = fondo de escala) */
float oki6295_generate(Oki6295 *o);

#endif
