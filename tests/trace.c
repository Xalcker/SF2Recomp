/*
 * trace.c - huellas de una partida para compararlas entre plataformas.
 *
 *   sf2_trace <sf2.zip|carpeta> <entradas.txt> <cuadros> [cada [salida.wav]]
 *
 * Corre hasta `cuadros` cuadros del juego (entregas de la IRQ2) y, cada `cada` cuadros (500 por omision), imprime el CRC-32 del estado completo de la maquina (CPU, RAM, registros de
 * video, placa de sonido con sus chips), del cuadro de video y del audio mezclado desde la linea anterior.  El estado y
 * el video son enteros y tienen que coincidir en toda plataforma; el audio es float y coincide bit a bit solo donde el
 * compilador contrae a FMA igual que MAME (arm64), en las demas debe pasar los criterios de energia y espectro (D33).
 */
#include <stdio.h>
#include <stdlib.h>
#include "crc32.h"
#include "hostio.h"
#include "romset.h"
#include "sf2.h"
#include "wav.h"

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "uso: %s <sf2.zip|carpeta> <entradas.txt> <cuadros> [cada]\n", argv[0]); return 2; }
    unsigned long frames = strtoul(argv[3], NULL, 0), every = argc > 4 ? strtoul(argv[4], NULL, 0) : 500;
    RomSet rs;
    char report[2048], err[512];
    romset_init(&rs);
    if (hostio_load_romset(argv[1], &rs, report, sizeof report) != 0) { fprintf(stderr, "%s", report); return 1; }
    Sf2 *s;
    if (sf2_create(&s, &rs, NULL, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 1; }
    romset_free(&rs);
    uint8_t *script;
    size_t len;
    if (hostio_read_file(argv[2], &script, &len) != 0 || sf2_load_input_script(s, (const char *)script, argv[2], err, sizeof err) < 0) {
        fprintf(stderr, "entradas: %s\n", err);
        return 1;
    }
    free(script);
    uint32_t audio = 0;
    WavWriter wav = { 0 };
    if (argc > 5 && wav_open(&wav, argv[5], SF2_AUDIO_RATE, 1) < 0) { fprintf(stderr, "no se puede escribir %s\n", argv[5]); return 1; }
    unsigned long next = every;
    for (;;) {
        if (sf2_run_frame(s) < 0) { fprintf(stderr, "error: %s\n", sf2_error(s)); return 1; }
        size_t n;
        const float *a = sf2_audio(s, &n);
        audio = sf2_crc32(audio, a, n * sizeof(float));
        if (wav.f) wav_add_float(&wav, a, (unsigned)n);
        sf2_audio_clear(s);
        unsigned long f = (unsigned long)sf2_frame_count(s);
        if (f >= next || f >= frames) {
            printf("cuadro=%-6lu estado=%08x video=%08x audio=%08x\n", f, sf2_state_crc(s),
                   sf2_crc32(0, sf2_frame(s), SF2_SCREEN_W * SF2_SCREEN_H * 4), audio);
            while (next <= f) next += every;
        }
        if (f >= frames) break;
    }
    if (wav.f) wav_close(&wav);
    sf2_destroy(s);
    return 0;
}
