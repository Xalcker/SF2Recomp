/* wav.c - escritor WAV PCM de 16 bits (ver wav.h). */
#include "wav.h"

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 }; fwrite(b, 1, 4, f); }
static void put16(FILE *f, uint16_t v) { uint8_t b[2] = { v, v >> 8 }; fwrite(b, 1, 2, f); }

static void header(WavWriter *w) {
    uint32_t bytes = (uint32_t)(w->frames * w->channels * 2);
    fseek(w->f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, w->f); put32(w->f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, w->f); put32(w->f, 16);
    put16(w->f, 1); put16(w->f, w->channels); put32(w->f, w->rate);
    put32(w->f, w->rate * w->channels * 2); put16(w->f, (uint16_t)(w->channels * 2)); put16(w->f, 16);
    fwrite("data", 1, 4, w->f); put32(w->f, bytes);
}

int wav_open(WavWriter *w, const char *path, uint32_t rate, uint16_t channels) {
    w->f = fopen(path, "wb");
    if (!w->f) return -1;
    w->rate = rate;
    w->channels = channels;
    w->frames = 0;
    header(w);
    return 0;
}

void wav_add_float(WavWriter *w, const float *x, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        int v = (int)(x[i] * 32768);
        if (v < -32768) v = -32768;
        if (v > 32767) v = 32767;
        put16(w->f, (uint16_t)(int16_t)v);
    }
    w->frames += n / w->channels;
}

void wav_close(WavWriter *w) {
    if (!w->f) return;
    header(w);
    fclose(w->f);
    w->f = NULL;
}
