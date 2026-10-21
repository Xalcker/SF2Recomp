/*
 * wav.h - escritor WAV PCM de 16 bits mínimo (mono o estéreo), sin dependencias.
 *
 * wav_add_float convierte como la grabación de MAME (sound_manager::output_push): int(x * 32768) saturado a
 * [-32768, 32767], así que la mezcla del runtime y un -wavwrite de MAME dan el mismo archivo.
 */
#ifndef RUNTIME_WAV_H
#define RUNTIME_WAV_H

#include <stdint.h>
#include <stdio.h>

typedef struct WavWriter {
    FILE *f;
    uint32_t rate;
    uint16_t channels;
    uint64_t frames;
} WavWriter;

int wav_open(WavWriter *w, const char *path, uint32_t rate, uint16_t channels);
void wav_add_float(WavWriter *w, const float *x, unsigned n);   /* n muestras (intercaladas si estéreo) */
void wav_close(WavWriter *w);

#endif
