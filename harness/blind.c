/*
 * blind.c - arnés "a ciegas" (hito 2): la máquina CPS1 mínima sobre el código recompilado, sin sonido,
 * con Musashi como red de seguridad para direcciones no recompiladas (harness/harness.c) y, desde el
 * hito 3, con el vídeo opcional para escribir PNG de cuadros elegidos sin ventana.
 *
 *   sf2_blind rom.bin [--frames N] [--inputs archivo] [--summary archivo] [--detail]
 *             [--dump-frame N --dump-dir dir] [--io-table archivo] [--frame-cycles N]
 *             [--insns N] [--cov archivo] [--log-io] [--quiet] [--step]
 *             [--gfx dir] [--png-frames a,b,c] [--png-dir dir] [--png-prefix nombre]
 *             [--audio] [--audio-dir dir] [--wav archivo] [--sound-roms dir] [--sound-cmds archivo]
 *             [--roms zip|dir]
 *
 * Con --png-frames se cargan las ROMs gráficas (--gfx, por defecto sf2) y se renderiza cada VBlank; en el
 * punto de muestra de cada cuadro de la lista se escribe <png-dir>/<prefijo>_N.png (384 x 224 RGB), la
 * misma imagen que tools/mame_snap.lua guarda desde MAME en ese cuadro.
 *
 * Sonido (hito 4): --audio carga la placa de sonido (sf2_09.bin, sf2_18.bin y sf2_19.bin de --sound-roms, por
 * defecto sf2).  --audio-dir escribe en ese directorio lo mismo que tools/mame_audio.lua (events.txt, ym.f32,
 * oki.f32, mono.f32, chunks.txt) y --wav la mezcla de 48 kHz como WAV de 16 bits; ambos activan --audio.
 * --sound-cmds escribe solo los comandos del 68000 a los latches, una línea "cuadro latch valor" por escritura
 * (el mismo formato que tools/mame_sound_cmds.lua); no necesita la placa de sonido ni sus ROMs.
 *
 * ROMs (hito 5): --gfx y --sound-roms aceptan una carpeta o un zip (como --roms, que fija los dos a la vez).
 * --step ejecuta con machine_step (cediendo tras cada VBlank, como el juego SDL3 y el navegador) en vez de
 * machine_run: el resumen tiene que salir idéntico.
 *
 * Hito 6: sin --fallback una dirección sin función recompilada detiene la máquina con el mismo mensaje que el
 * juego (código de salida 2); con --fallback la interpreta Musashi y se registra el episodio (D19).  --dispatch-log
 * escribe cada par (instrucción de origen, destino) despachado por la tabla con su cuenta, para tools/coverage.py.
 * --dips fija los dipswitches (DSWA,DSWB,DSWC en hexadecimal; por omisión ff,fc,9f como MAME).
 * --roundtrip a,b,c (implica --step): en esos cuadros guarda el estado, reinicia la máquina y lo carga; el resumen tiene
 * que salir idéntico al de la partida seguida (tools/coverage.py --roundtrip lo comprueba en todas las secuencias).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"
#include "common.h"
#include "hostio.h"
#include "gfx.h"
#include "video.h"
#include "sound.h"
#include "wav.h"
#include "state.h"
#include "crc32.h"

static Machine g_mc;
static Gfx g_gfx;
static Video *g_video;
static uint64_t *g_png_frames;
static int g_npng;
static const char *g_png_dir = ".", *g_png_prefix = "cuadro";
static int g_png_written;
static Cps1Sound g_sound;
static WavWriter g_wav;
static uint64_t g_dump_frame;
static const char *g_dump_dir;

static void on_audio(void *user, const float *mono, unsigned n) {
    (void)user;
    if (g_wav.f) wav_add_float(&g_wav, mono, n);
}

static FILE *open_out(const char *dir, const char *name, const char *mode) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, mode);
    if (!f) perror(path);
    return f;
}

static void on_vblank(Machine *mc) {
    video_vblank(g_video, &mc->mem);
}

static void dump_ram(Machine *mc, uint64_t frame) {
    char path[1024];
    snprintf(path, sizeof path, "%s/wram_%llu.bin", g_dump_dir ? g_dump_dir : ".", (unsigned long long)frame);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(mc->mem.wram, 1, WRAM_SIZE, f); fclose(f); fprintf(stderr, "RAM de trabajo del cuadro %llu en %s\n", (unsigned long long)frame, path); }
    snprintf(path, sizeof path, "%s/gfx_%llu.bin", g_dump_dir ? g_dump_dir : ".", (unsigned long long)frame);
    f = fopen(path, "wb");
    if (f) { fwrite(mc->mem.gfxram, 1, GFXRAM_SIZE, f); fclose(f); fprintf(stderr, "RAM grafica del cuadro %llu en %s\n", (unsigned long long)frame, path); }
}

static void on_sample(Machine *mc, uint64_t frame) {
    harness_progress(mc, frame);
    if (g_dump_frame && frame == g_dump_frame)
        dump_ram(mc, frame);
    for (int i = 0; i < g_npng; i++) {
        if (g_png_frames[i] != frame) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s_%llu.png", g_png_dir, g_png_prefix, (unsigned long long)frame);
        if (hostio_write_video_png(g_video, path) == 0) {
            g_png_written++;
            if (!mc->quiet) {
                const Mem *m = &mc->mem;
                fprintf(stderr, "  cuadro %llu: %s (orden de capas %d,%d,%d,%d; habilitadas %d%d%d; %d sprites; rowscroll %d)\n",
                        (unsigned long long)frame, path, g_video->order[0], g_video->order[1], g_video->order[2],
                        g_video->order[3], g_video->enabled[1], g_video->enabled[2], g_video->enabled[3], g_video->sprites,
                        g_video->rowscroll);
                fprintf(stderr, "    prioridad: %d pixeles marcados, %d pixeles de sprite tapados\n", g_video->marked, g_video->blocked);
                fprintf(stderr, "    CPS-A: obj=%04x scr1=%04x scr2=%04x scr3=%04x other=%04x pal=%04x "
                        "scroll1=%04x,%04x scroll2=%04x,%04x scroll3=%04x,%04x rowoffs=%04x vidctrl=%04x\n",
                        m->cps_a[0], m->cps_a[1], m->cps_a[2], m->cps_a[3], m->cps_a[4], m->cps_a[5], m->cps_a[6], m->cps_a[7],
                        m->cps_a[8], m->cps_a[9], m->cps_a[10], m->cps_a[11], m->cps_a[0x10], m->cps_a[0x11]);
                fprintf(stderr, "    CPS-B: layerctrl=%04x prio=%04x,%04x,%04x,%04x palctrl=%04x\n", m->cps_b[0x13],
                        m->cps_b[0x14], m->cps_b[0x15], m->cps_b[0x16], m->cps_b[0x17], m->cps_b[0x18]);
            }
        }
    }
}

static void on_finish(Machine *mc, const char *why) {
    (void)why;
    if (mc->sound) {
        Cps1Sound *snd = mc->sound;
        FILE **fs[] = { &snd->f_events, &snd->f_ym, &snd->f_oki, &snd->f_mono, &snd->f_chunks };
        for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++)
            if (*fs[i]) { fclose(*fs[i]); *fs[i] = NULL; }
        if (g_wav.f) {
            printf("sonido: %llu muestras en el WAV\n", (unsigned long long)g_wav.frames);
            wav_close(&g_wav);
        }
    }
    if (g_video)
        printf("video: %llu cuadros renderizados, %d PNG escritos en %s, %llu copias de paleta\n",
               (unsigned long long)g_video->renders, g_png_written, g_png_dir, (unsigned long long)mc->mem.palette_copies);
}

/* --roundtrip (hito 6): guarda el estado completo, apaga y enciende la máquina (RAM a cero, CPU en reset, placa de
 * sonido en reset), carga el estado y sigue.  El resumen por cuadro tiene que salir idéntico al de la partida seguida:
 * el estado guardado contiene todo lo que la partida necesita.  Se hace en un punto de cesión (--step), como el juego. */
static size_t state_payload(Machine *mc, uint8_t *buf, size_t cap) {
    StateStream s;
    state_begin_save(&s, buf, cap);
    machine_state(mc, &s);
    if (g_video)
        video_state(g_video, &s);
    return s.error ? 0 : s.pos;
}

static int roundtrip(Machine *mc) {
    size_t n = state_payload(mc, NULL, 0);
    uint8_t *buf = malloc(n), *again = malloc(n);
    if (!buf || !again || state_payload(mc, buf, n) != n) { free(buf); free(again); return -1; }
    uint32_t crc = sf2_crc32(0, buf, n);
    uint64_t frame = mc->cps.frame;
    machine_reset(mc);                          /* olvidar todo lo que no esté en el estado */
    memset(mc->mem.wram, 0xA5, WRAM_SIZE);
    memset(mc->mem.gfxram, 0x5A, GFXRAM_SIZE);
    StateStream s;
    state_begin_load(&s, buf, n);
    machine_state(mc, &s);
    if (g_video)
        video_state(g_video, &s);
    int ok = !s.error && s.pos == n && state_payload(mc, again, n) == n && !memcmp(buf, again, n);
    if (!mc->quiet || !ok)
        fprintf(stderr, "--roundtrip: cuadro %llu, estado de %zu bytes (CRC %08x) guardado, maquina reiniciada y estado "
                "cargado%s\n", (unsigned long long)frame, n, crc, ok ? "" : ": ERROR, no se recupera el mismo estado");
    free(buf);
    free(again);
    return ok ? 0 : -1;
}

static int parse_frame_list(const char *s, uint64_t **out) {
    int n = 0, cap = 0;
    *out = NULL;
    while (*s) {
        char *end;
        unsigned long long v = strtoull(s, &end, 10);
        if (end == s) return -1;
        if (n == cap) { cap = cap ? cap * 2 : 16; *out = realloc(*out, cap * sizeof **out); }
        (*out)[n++] = v;
        s = end;
        while (*s == ',' || *s == ' ') s++;
    }
    return n;
}

/* carga las ROMs de una carpeta o de un zip y avisa de lo que falte */
static int load_roms(const char *path, RomSet *rs) {
    char report[2048];
    romset_init(rs);
    if (hostio_load_romset(path, rs, report, sizeof report) != 0) {
        fprintf(stderr, "%s: %s", path, report[0] ? report : "no se pudieron cargar las ROMs\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Machine *mc = &g_mc;
    const char *rom_path = NULL, *inputs = NULL, *summary = NULL, *gfx_dir = "sf2", *png_list = NULL;
    const char *cov_path = NULL, *io_table = NULL;
    uint64_t frame_cycles = CPS1_FRAME_CYCLES, max_frames = 600, max_insns = 0;
    int log_io = 0, detail = 0, audio = 0, quiet = 0, step = 0;
    const char *audio_dir = NULL, *wav_path = NULL, *sound_roms = "sf2", *cmds_path = NULL;
    const char *dispatch_path = NULL, *dips = NULL, *roundtrip_list = NULL;
    int fallback = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--insns") && i + 1 < argc) max_insns = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--inputs") && i + 1 < argc) inputs = argv[++i];
        else if (!strcmp(argv[i], "--summary") && i + 1 < argc) summary = argv[++i];
        else if (!strcmp(argv[i], "--detail")) detail = 1;
        else if (!strcmp(argv[i], "--dump-frame") && i + 1 < argc) g_dump_frame = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc) g_dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--io-table") && i + 1 < argc) io_table = argv[++i];
        else if (!strcmp(argv[i], "--frame-cycles") && i + 1 < argc) frame_cycles = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--step")) step = 1;
        else if (!strcmp(argv[i], "--log-io")) log_io = 1;
        else if (!strcmp(argv[i], "--cov") && i + 1 < argc) cov_path = argv[++i];
        else if (!strcmp(argv[i], "--gfx") && i + 1 < argc) gfx_dir = argv[++i];
        else if (!strcmp(argv[i], "--roms") && i + 1 < argc) gfx_dir = sound_roms = argv[++i];
        else if (!strcmp(argv[i], "--png-frames") && i + 1 < argc) png_list = argv[++i];
        else if (!strcmp(argv[i], "--png-dir") && i + 1 < argc) g_png_dir = argv[++i];
        else if (!strcmp(argv[i], "--png-prefix") && i + 1 < argc) g_png_prefix = argv[++i];
        else if (!strcmp(argv[i], "--audio")) audio = 1;
        else if (!strcmp(argv[i], "--audio-dir") && i + 1 < argc) { audio_dir = argv[++i]; audio = 1; }
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) { wav_path = argv[++i]; audio = 1; }
        else if (!strcmp(argv[i], "--sound-roms") && i + 1 < argc) sound_roms = argv[++i];
        else if (!strcmp(argv[i], "--sound-cmds") && i + 1 < argc) cmds_path = argv[++i];
        else if (!strcmp(argv[i], "--dispatch-log") && i + 1 < argc) dispatch_path = argv[++i];
        else if (!strcmp(argv[i], "--dips") && i + 1 < argc) dips = argv[++i];
        else if (!strcmp(argv[i], "--fallback")) fallback = 1;
        else if (!strcmp(argv[i], "--roundtrip") && i + 1 < argc) { roundtrip_list = argv[++i]; step = 1; }
        else if (!strcmp(argv[i], "--trace-cycles") && i + 2 < argc) {
            unsigned long long a = 0, b = 0;
            if (sscanf(argv[i + 1], "%llu:%llu", &a, &b) != 2) { fprintf(stderr, "--trace-cycles desde:hasta archivo\n"); return 1; }
            FILE *tf = fopen(argv[i + 2], "w");
            if (!tf) { perror(argv[i + 2]); return 1; }
            harness_set_cycle_trace(tf, a, b);
            i += 2;
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "opcion desconocida: %s\n", argv[i]); return 1; }
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "uso: %s rom.bin [--frames N] [--inputs archivo] [--summary archivo] [--detail]\n"
                        "        [--dump-frame N --dump-dir dir] [--io-table archivo] [--frame-cycles N]\n"
                        "        [--insns N] [--cov archivo] [--log-io] [--quiet] [--step]\n"
                        "        [--gfx dir|zip] [--png-frames a,b,c] [--png-dir dir] [--png-prefix nombre]\n"
                        "        [--audio] [--audio-dir dir] [--wav archivo] [--sound-roms dir|zip] [--sound-cmds archivo]\n"
                        "        [--roms dir|zip] [--dips a,b,c] [--dispatch-log archivo] [--fallback] [--roundtrip a,b,c]\n", argv[0]);
        return 1;
    }
    uint8_t *rom = load_rom(rom_path);
    if (!rom) return 1;

    machine_init(mc, rom, frame_cycles);
    mc->max_frames = max_frames;
    mc->max_insns = max_insns;
    mc->quiet = quiet;
    harness_install(mc, fallback);
    harness_set_report_paths(cov_path, io_table);
    harness_set_dispatch_log(mc, dispatch_path);
    if (dips) {
        unsigned a, b, c;
        if (sscanf(dips, "%x,%x,%x", &a, &b, &c) != 3 || a > 0xff || b > 0xff || c > 0xff) {
            fprintf(stderr, "--dips: se esperan tres bytes en hexadecimal, p. ej. ff,fc,9f\n");
            return 1;
        }
        mc->mem.dswa = (uint16_t)a;
        mc->mem.dswb = (uint16_t)b;
        mc->mem.dswc = (uint16_t)c;
    }
    mc->mem.log_io = log_io;
    mc->mem.log_file = stderr;
    mc->cps.detail = detail;
    mc->on_sample = on_sample;
    if (inputs) {
        int n = cps1_load_inputs(&mc->cps, inputs);
        if (n < 0) { fprintf(stderr, "no se pudo leer la secuencia de entradas %s\n", inputs); return 1; }
        if (!mc->quiet) fprintf(stderr, "entradas: %d eventos de %s\n", n, inputs);
    }
    if (summary) {
        mc->cps.summary = !strcmp(summary, "-") ? stdout : fopen(summary, "w");
        if (!mc->cps.summary) { perror(summary); return 1; }
    }
    if (png_list) {
        RomSet rs;
        g_npng = parse_frame_list(png_list, &g_png_frames);
        if (g_npng < 0) { fprintf(stderr, "--png-frames: lista de cuadros invalida\n"); return 1; }
        if (load_roms(gfx_dir, &rs) < 0 || gfx_init(&g_gfx, &rs) < 0) { fprintf(stderr, "no se pudieron cargar las ROMs graficas de %s\n", gfx_dir); return 1; }
        romset_free(&rs);
        g_video = malloc(sizeof *g_video);
        video_init(g_video, &g_gfx);
        const char *layers = getenv("SF2_VIDEO_LAYERS");     /* máscara de depuración: bit 0 sprites, bits 1-3 SCROLL1-3 */
        if (layers) g_video->layer_mask = (int)strtol(layers, NULL, 0) & 0xf;
        mc->on_vblank = on_vblank;
        if (!mc->quiet) fprintf(stderr, "video: ROMs graficas de %s, %d cuadros a PNG en %s\n", gfx_dir, g_npng, g_png_dir);
    }
    if (audio) {
        RomSet rs;
        if (load_roms(sound_roms, &rs) < 0 || sound_init(&g_sound, &rs) < 0) { fprintf(stderr, "no se pudo preparar la placa de sonido (ROMs en %s)\n", sound_roms); return 1; }
        romset_free(&rs);
        const char *trace = getenv("SF2_Z80_TRACE");        /* depuración: desde:hasta (ciclos del Z80) */
        unsigned long long ta = 0, tb = 0;
        if (trace && sscanf(trace, "%llu:%llu", &ta, &tb) == 2) sound_set_trace(&g_sound, stderr, ta, tb);
        if (audio_dir) {
            g_sound.f_events = open_out(audio_dir, "events.txt", "w");
            g_sound.f_ym = open_out(audio_dir, "ym.f32", "wb");
            g_sound.f_oki = open_out(audio_dir, "oki.f32", "wb");
            g_sound.f_mono = open_out(audio_dir, "mono.f32", "wb");
            g_sound.f_chunks = open_out(audio_dir, "chunks.txt", "w");
            if (!g_sound.f_events || !g_sound.f_ym || !g_sound.f_oki || !g_sound.f_mono || !g_sound.f_chunks) return 1;
        }
        if (wav_path && wav_open(&g_wav, wav_path, SOUND_OUT_RATE, 1) < 0) { perror(wav_path); return 1; }
        g_sound.on_audio = on_audio;
        machine_attach_sound(mc, &g_sound);
        if (!mc->quiet) fprintf(stderr, "sonido: placa CPS1 con las ROMs de %s%s%s%s%s\n", sound_roms,
                                audio_dir ? ", registros en " : "", audio_dir ? audio_dir : "",
                                wav_path ? ", WAV en " : "", wav_path ? wav_path : "");
    }
    if (cmds_path) {
        FILE *f = fopen(cmds_path, "w");
        if (!f) { perror(cmds_path); return 1; }
        machine_log_sound_cmds(mc, f);
        if (!mc->quiet) fprintf(stderr, "sonido: comandos de los latches en %s\n", cmds_path);
    }
    if (step) {
        /* misma partida, pero devolviendo el control al anfitrión tras cada VBlank (como el juego SDL3 y el navegador) */
        unsigned long long steps = 0;
        int st;
        uint64_t *rt = NULL;
        int nrt = roundtrip_list ? parse_frame_list(roundtrip_list, &rt) : 0, irt = 0;
        if (nrt < 0) { fprintf(stderr, "--roundtrip: lista de cuadros invalida\n"); return 1; }
        while ((st = machine_step(mc)) == MACHINE_YIELD) {
            steps++;
            while (irt < nrt && mc->cps.frame >= rt[irt]) {
                if (roundtrip(mc) != 0) { fprintf(stderr, "--roundtrip: el estado cargado no es el guardado\n"); return 3; }
                irt++;
            }
        }
        if (nrt && !mc->quiet) fprintf(stderr, "--roundtrip: %d de %d puntos hechos\n", irt, nrt);
        free(rt);
        if (!mc->quiet) fprintf(stderr, "--step: %llu cesiones para %llu VBlank\n", steps, (unsigned long long)mc->vblanks);
        if (st == MACHINE_ERROR) mc->status = MACHINE_ERROR;
    }
    harness_run(mc, on_finish);
    return 0;
}
