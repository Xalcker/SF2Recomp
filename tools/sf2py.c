/*
 * sf2py.c - biblioteca compartida mínima sobre la fachada del núcleo (runtime/sf2.h) para manejar la máquina desde
 * Python con ctypes (tools/playgen.py, hito 6): crear la máquina con las ROMs de una carpeta o un zip, fijar las
 * entradas del cuadro siguiente, correr cuadros, leer la RAM de trabajo y guardar/cargar estados en memoria.
 *
 * No es parte del juego: es una herramienta para generar y estudiar secuencias de entradas.  Las entradas que se
 * fijan con sf2py_set_input se aplican al entregar la IRQ2 del cuadro siguiente (cps1_apply_inputs), exactamente
 * como una línea "desde hasta tecla" de una secuencia de tools/: lo que se graba aquí se reproduce igual en
 * sf2_blind y en MAME.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hostio.h"
#include "machine.h"
#include "sf2.h"
#include "video.h"

#if defined(_WIN32)
#define SF2PY_API __declspec(dllexport)
#else
#define SF2PY_API __attribute__((visibility("default")))
#endif

/* Sf2 empieza por su Machine (runtime/sf2.c: "primer miembro (ganchos y depuración)") */
static Machine *mach(Sf2 *s) { return (Machine *)s; }

SF2PY_API Sf2 *sf2py_create(const char *roms, int video, int sound, char *err, int errn) {
    RomSet rs;
    char report[2048];
    romset_init(&rs);
    if (hostio_load_romset(roms, &rs, report, sizeof report) != 0) {
        snprintf(err, (size_t)errn, "%s: %s", roms, report[0] ? report : "no se pudieron cargar las ROMs");
        romset_free(&rs);
        return NULL;
    }
    Sf2Options o;
    sf2_options_default(&o);
    o.video = video;
    o.sound = sound;
    Sf2 *s = NULL;
    if (sf2_create(&s, &rs, &o, err, (size_t)errn) < 0)
        s = NULL;
    romset_free(&rs);
    return s;
}

SF2PY_API void sf2py_destroy(Sf2 *s) { sf2_destroy(s); }
SF2PY_API void sf2py_set_input(Sf2 *s, int in0, int in1, int in2) { sf2_set_input(s, (uint16_t)in0, (uint16_t)in1, (uint16_t)in2); }
SF2PY_API void sf2py_set_dips(Sf2 *s, int a, int b, int c) { sf2_set_dips(s, (uint8_t)a, (uint8_t)b, (uint8_t)c); }
SF2PY_API const char *sf2py_error(Sf2 *s) { return sf2_error(s); }
SF2PY_API unsigned long long sf2py_frame(Sf2 *s) { return sf2_frame_count(s); }
SF2PY_API unsigned long long sf2py_vblanks(Sf2 *s) { return sf2_vblank_count(s); }
SF2PY_API unsigned long long sf2py_insns(Sf2 *s) { return mach(s)->cpu.icount; }
SF2PY_API uint8_t *sf2py_wram(Sf2 *s) { return mach(s)->mem.wram; }
SF2PY_API uint8_t *sf2py_gfxram(Sf2 *s) { return mach(s)->mem.gfxram; }
SF2PY_API const uint32_t *sf2py_rgb(Sf2 *s) { return sf2_frame(s); }

/* corre hasta que el contador de cuadros del juego avance `n` (las entradas fijadas se mantienen); devuelve los
 * cuadros corridos o -1 si el juego falló */
SF2PY_API int sf2py_run(Sf2 *s, int n) {
    uint64_t target = sf2_frame_count(s) + (uint64_t)n;
    int guard = 0;
    while (sf2_frame_count(s) < target) {
        if (sf2_run_frame(s) < 0)
            return -1;
        if (++guard > 4 * n + 1000)       /* IRQ enmascarada mucho tiempo: devolver para no colgar */
            break;
    }
    return (int)(sf2_frame_count(s) - (target - (uint64_t)n));
}

/* un VBlank (como sf2_run_frame) */
SF2PY_API int sf2py_run_vblank(Sf2 *s) { return sf2_run_frame(s); }

SF2PY_API int sf2py_state_size(Sf2 *s) { return (int)sf2_state_size(s); }
SF2PY_API int sf2py_state_save(Sf2 *s, uint8_t *buf, int cap) { return sf2_state_save(s, buf, (size_t)cap); }
SF2PY_API int sf2py_state_load(Sf2 *s, const uint8_t *buf, int len, char *err, int errn) {
    return sf2_state_load(s, buf, (size_t)len, err, (size_t)errn);
}
SF2PY_API unsigned sf2py_state_crc(Sf2 *s) { return sf2_state_crc(s); }

SF2PY_API int sf2py_png(Sf2 *s, const char *path) {
    return hostio_write_png(path, sf2_frame(s), SF2_SCREEN_W, SF2_SCREEN_H);
}

/* mod de entrenamiento (runtime/training.h): devuelve 0 o -1 con el motivo en err */
SF2PY_API int sf2py_training(Sf2 *s, int enabled, int infinite_time, int refill, int refill_delay, int lock_p2, int auto_vs, int dummy,
                             const char *script, int track_inputs, char *err, int errn) {
    Sf2Training t;
    memset(&t, 0, sizeof t);
    t.enabled = enabled;
    t.infinite_time = infinite_time;
    t.refill = refill;
    t.refill_delay = refill_delay;
    t.lock_p2 = lock_p2;
    t.auto_vs = auto_vs;
    t.dummy = dummy;
    t.track_inputs = track_inputs;
    snprintf(t.script, sizeof t.script, "%s", script ? script : "");
    return sf2_training_set(s, &t, err, (size_t)errn);
}
/* historial: escribe pares (mascara, cuadros) como enteros de 32 bits, mas reciente primero; devuelve cuantos */
SF2PY_API int sf2py_training_history(Sf2 *s, int player, uint32_t *out, int max) {
    Sf2InputEvent ev[SF2_TRAIN_HISTORY];
    int n = sf2_training_history(s, player, ev, max < SF2_TRAIN_HISTORY ? max : SF2_TRAIN_HISTORY);
    for (int i = 0; i < n; i++) { out[2 * i] = ev[i].mask; out[2 * i + 1] = ev[i].frames; }
    return n;
}
