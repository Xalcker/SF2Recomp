/*
 * main.c - el juego: SF2Recomp sobre SDL3 (ventana, teclado y mandos, audio, archivos).
 *
 * Es la única parte del proyecto que habla con SDL: el núcleo (runtime/sf2.h) recibe las ROMs en memoria y las
 * entradas de cada cuadro y entrega imagen, audio y estados guardados.  Usa los callbacks de aplicación de
 * SDL3 (SDL_AppInit / SDL_AppIterate...), que en escritorio son el bucle de siempre y en el navegador
 * (Emscripten) son requestAnimationFrame, con el mismo código.
 *
 *   sf2 [sf2.zip|carpeta] [opciones]        (ver --help)
 *
 * Ritmo: con audio, lo marca el dispositivo de sonido (se emulan cuadros mientras la cola de audio tiene menos
 * de AUDIO_TARGET_MS), así el juego va a la velocidad exacta del reloj de audio sin descartar ni repetir
 * muestras; sin audio, un reloj de 59,637 Hz.  Con la tecla de avance rápido o --nothrottle se emula sin
 * esperar.
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "hostio.h"
#include "input.h"
#include "romset.h"
#include "sf2.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define APP_NAME        "SF2Recomp"
#define AUDIO_TARGET_MS 60
#define AUDIO_MAX_ITER  4          /* cuadros máximos por vuelta del bucle para no bloquear la ventana */
#define OSD_MS          2500
#define FF_BUDGET_NS    12000000ull /* avance rápido: tiempo de emulación por vuelta */
#define RESCUE_EVERY    600        /* cuadros entre copias en memoria del estado (para el informe de un fallo) */

typedef enum BenchMode { BENCH_OFF, BENCH_CORE, BENCH_VIDEO, BENCH_FULL } BenchMode;

typedef struct App {
    Sf2Config cfg;
    char cfg_path[1024], pref_dir[1024], state_dir[1024], shot_dir[1024];
    Sf2 *sf2;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioDeviceID audio_dev;
    SDL_AudioStream *audio;
    Input *input;
    int slot;
    int paused, halted, nothrottle, fast, new_frame;
    uint64_t frame_ns, next_ns;
    uint64_t frames_run, max_frames;
    int dev_sel;                     /* herramienta de desarrollo elegida en el menú (se aplica con Enter) */
    char osd[200];
    uint64_t osd_until_ns;
    uint64_t stat_t0_ns, stat_frames, stat_draws;
    /* HFR (docs/HFR.md): instante en que llegó el último cuadro del juego, último t dibujado y reparto de los t dibujados (--stats) */
    uint64_t last_draw_ns;             /* instante del último dibujado (sin vsync y sin HFR solo se dibuja cuando hay algo nuevo) */
    uint64_t frame_arrive_ns, hfr_draws, hfr_hist[8], present_ns, present_n;
    int hfr_t;
    double measured_hz;                /* cuadros que se presentaron por segundo en la última ventana de 1 s */
    const char *shot_png;              /* --shot-png RUTA:CUADRO (pruebas): guarda la ventana con los visores a ese cuadro y termina */
    uint64_t shot_frame;
    const char *menu_png;              /* --menu-png (pruebas): abre el menú tras unos cuadros, lo guarda como PNG y termina */
    int menu_shot_done, menu_shot_page;
    int menu_test, menu_test_step;     /* --menu-test (pruebas): teclea una secuencia en el menú y comprueba el resultado */
    int menu, menu_sel, menu_page, menu_sel_page[8];   /* menú: abierto, opción y página; la opción elegida de cada página */
    int bind_player, bind_pad, capture;                /* controles: jugador 0/1, 0 teclado/1 mando, acción esperando una tecla (-1 ninguna) */                /* menú en el juego (docs/MENU.md): abierto y opción elegida */
    int soft_pace;                     /* 1: el vsync del sistema presentaba a 60 Hz en un monitor más rápido y se pasó a reloj propio */
    uint64_t next_draw_ns;             /* sin vsync: instante del próximo dibujado (se sigue el refresco del monitor) */
    double refresh_hz;                 /* refresco del monitor donde está la ventana (0 = no se sabe) */
    double fps;
    /* medidas */
    BenchMode bench;
    uint64_t bench_target, bench_frames, bench_t0;
    /* estadisticas del ritmo por audio (--stats) */
    int stats;
    int aq_min_ms, aq_max_ms;
    uint64_t iters, underruns, frames_by_iter[5];
    uint64_t multi_vblank_runs;       /* vueltas de sf2_run_frame que pasaron mas de un VBlank (no hay punto de cesion) */
    int max_vblanks_run;
    /* capturas de cuadros concretos (pruebas) */
    uint64_t *png_frames;
    int npng;
    const char *png_dir;
    int quiet;
    /* registro de fallos (hito 6): copia del estado cada RESCUE_EVERY cuadros, que se escribe junto al informe si el
     * código recompilado se detiene (dirección que el recompilador no vio u otro error) */
    uint8_t *rescue;
    size_t rescue_cap, rescue_len;
    uint64_t rescue_frame;
    uint64_t debug_jump_frame;        /* --debug-jump N:DIR (pruebas): en el cuadro N el 68000 salta a DIR */
    uint32_t debug_jump_pc;
} App;

static App g_app;

/* ---- utilidades ------------------------------------------------------------------------------------------ */
static void osd(App *a, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
static void osd(App *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->osd, sizeof a->osd, fmt, ap);
    va_end(ap);
    a->osd_until_ns = SDL_GetTicksNS() + (uint64_t)OSD_MS * 1000000ull;
    if (!a->quiet)
        fprintf(stderr, "%s\n", a->osd);
}

static void persist_flush(void) {
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.sf2Persist) FS.syncfs(false, function (e) { if (e) console.error(e); }); });
#endif
}

static void fatal_message(const char *title, const char *msg) {
    fprintf(stderr, "%s: %s\n", title, msg);
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.sf2Fatal) Module.sf2Fatal(UTF8ToString($0)); }, msg);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
#endif
}

static int path_exists(const char *p) {
    SDL_PathInfo info;
    return p && *p && SDL_GetPathInfo(p, &info);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "uso: %s [sf2.zip|carpeta] [opciones]\n"
        "  --config ARCHIVO   archivo de configuracion (por omision el de preferencias del usuario)\n"
        "  --rom RUTA         sf2.zip o carpeta con las ROMs (tambien como primer argumento)\n"
        "  --scale N          ventana de 384*N x 224*N (0 = ajustar)     --fullscreen\n"
        "  --training         mod de entrenamiento (docs/ENTRENAMIENTO.md): sin limite de tiempo, barras que se recuperan, J2 como dummy y versus automatico\n"
        "  --dummy MODO       stand|crouch|jump|block|block_crouch|walk_forward|walk_back|script    --dummy-script \"d:30 df:3 f+lp:2 n:30\"\n"
        "  --show-inputs      visor de las entradas de J1 y J2    --show-hitboxes   cajas de golpe y de dano\n"
        "  --widescreen       pantalla ancha real (mas escenario a los lados en combate, sin estirar)   --hud-edges\n"
        "  --hfr              cuadros intermedios al refresco del monitor (interpola camara y sprites; no cambia el juego)\n"
        "  --dev-tool 1-2     abre una herramienta de desarrollo oculta de Capcom (1 scroll, 2 OBJ TEST); cambia el juego\n"
        "  --wide-logic       con pantalla ancha, paredes y camara del juego con el ancho nuevo (cambia el juego)\n"
        "  --volume 0-100     --mute                                     --nothrottle\n"
        "  --inputs ARCHIVO   secuencia de entradas por cuadros (pruebas) --frames N   termina tras N cuadros\n"
        "  --load-state ARCH  carga un estado guardado al arrancar\n"
        "  --debug-jump N:DIR pruebas: en el cuadro N el 68000 salta a DIR (comprueba el registro de fallos)\n"
        "  --png-frames a,b   guarda esos cuadros como PNG en --png-dir (pruebas)\n"
        "  --bench N [--bench-mode core|video|full]  mide cuadros por segundo y termina\n"
        "  --write-config     escribe la configuracion con los valores actuales y termina\n"
        "  --stats            al terminar imprime la ocupacion de la cola de audio y los cuadros por vuelta\n"
        "Teclas por omision: flechas, Z X C = punetazos, A S D = patadas, 5 moneda, 1 start, F5/F7 guardar/cargar\n"
        "estado, F6/F8 ranura, P pausa, Tab avance rapido, F11 pantalla completa, F9 pantalla ancha, F10 reinicio, F12 captura.\n"
        "Todo se puede cambiar en el archivo de configuracion.\n", argv0);
}

/* ---- ROMs ---------------------------------------------------------------------------------------------------- */
/* origen de ROMs: el pedido expresamente (argumento, configuracion o SF2_ROM) o, si no hay, el primer sf2.zip que
 * existe en el directorio actual, junto al juego o en las preferencias.  Devuelve 1 si la ruta existe. */
static int find_rom_path(App *a, const char *arg, char *out, size_t n) {
    const char *asked[3] = { arg, a->cfg.rom_path, SDL_getenv("SF2_ROM") };
    for (int i = 0; i < 3; i++)
        if (asked[i] && *asked[i]) {
            snprintf(out, n, "%s", asked[i]);
            return path_exists(out);
        }
    const char *base = SDL_GetBasePath();
    char tried[3][1100];
    snprintf(tried[0], sizeof tried[0], "sf2.zip");
    snprintf(tried[1], sizeof tried[1], "%ssf2.zip", base ? base : "");
    snprintf(tried[2], sizeof tried[2], "%ssf2.zip", a->pref_dir);
    for (int i = 0; i < 3; i++)
        if (path_exists(tried[i])) {
            snprintf(out, n, "%s", tried[i]);
            return 1;
        }
    snprintf(out, n, "sf2.zip");
    return 0;
}

static int load_roms(App *a, const char *arg, RomSet *rs) {
    char path[1100], report[2600], msg[3200];
    int found = find_rom_path(a, arg, path, sizeof path);
    romset_init(rs);
    if (!found) {
        snprintf(msg, sizeof msg,
            "No se encuentran las ROMs de Street Fighter II (World 910522).\n\n"
            "Coloca sf2.zip (el del set \"sf2\" de MAME) junto al juego o en\n  %s\n"
            "o indica su ruta: sf2 /ruta/sf2.zip   (o [paths] rom = ... en %s).\n\n"
            "El proyecto no incluye ROMs: son las tuyas.", a->pref_dir, a->cfg_path);
        fatal_message(APP_NAME, msg);
        return -1;
    }
    int missing = hostio_load_romset(path, rs, report, sizeof report);
    if (missing != 0) {
        snprintf(msg, sizeof msg, "%s:\n\n%s", path, report[0] ? report : "no se pudieron leer las ROMs");
        fatal_message(APP_NAME, msg);
        romset_free(rs);
        return -1;
    }
    if (!a->quiet) fprintf(stderr, "ROMs: %s (23 de 23, CRC verificados)\n", path);
    return 0;
}

/* ---- estados guardados ---------------------------------------------------------------------------------------- */
static void state_path(const App *a, char *out, size_t n) {
    snprintf(out, n, "%s/sf2_slot%d.sta", a->state_dir, a->slot);
}

static void save_state(App *a) {
    size_t size = sf2_state_size(a->sf2);
    uint8_t *buf = malloc(size);
    char path[1200];
    state_path(a, path, sizeof path);
    SDL_CreateDirectory(a->state_dir);                 // la carpeta se crea al guardar por primera vez
    int n = buf ? sf2_state_save(a->sf2, buf, size) : -1;
    if (n <= 0) {
        osd(a, "No se pudo serializar el estado (%d de %d bytes)", n, (int)size);
    } else if (hostio_write_file(path, buf, (size_t)n) != 0) {
        osd(a, "No se pudo escribir el estado en %s: %s", path, strerror(errno));
    } else {
        persist_flush();
        osd(a, "Estado guardado en la ranura %d (%d KiB)", a->slot, (n + 1023) / 1024);
    }
    free(buf);
}

static int load_state_file(App *a, const char *path) {
    uint8_t *data;
    size_t len;
    char err[256];
    if (hostio_read_file(path, &data, &len) != 0) {
        osd(a, "No hay estado en %s", path);
        return -1;
    }
    int rc = sf2_state_load(a->sf2, data, len, err, sizeof err);
    free(data);
    if (rc != 0) {
        osd(a, "Estado rechazado: %s", err);
        return -1;
    }
    a->halted = 0;
    a->new_frame = 1;
    return 0;
}

static void load_state(App *a) {
    char path[1200];
    state_path(a, path, sizeof path);
    if (load_state_file(a, path) == 0)
        osd(a, "Estado cargado de la ranura %d", a->slot);
}

/* ---- audio --------------------------------------------------------------------------------------------------- */
static void audio_open(App *a) {
    if (!a->cfg.audio)
        return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "sin sonido (SDL audio: %s)\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec dev = { SDL_AUDIO_F32, 1, a->cfg.audio_rate }, src = { SDL_AUDIO_F32, 1, (int)SF2_AUDIO_RATE };
    a->audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dev);
    if (!a->audio_dev) {
        fprintf(stderr, "sin sonido (SDL_OpenAudioDevice: %s)\n", SDL_GetError());
        return;
    }
    a->audio = SDL_CreateAudioStream(&src, &dev);
    if (!a->audio || !SDL_BindAudioStream(a->audio_dev, a->audio)) {
        fprintf(stderr, "sin sonido (flujo de audio: %s)\n", SDL_GetError());
        if (a->audio) SDL_DestroyAudioStream(a->audio);
        a->audio = NULL;
        SDL_CloseAudioDevice(a->audio_dev);
        a->audio_dev = 0;
        return;
    }
    SDL_SetAudioStreamGain(a->audio, a->cfg.volume / 100.0f);
    SDL_ResumeAudioDevice(a->audio_dev);
}

static int audio_queued_ms(const App *a) {
    return (int)((int64_t)SDL_GetAudioStreamQueued(a->audio) * 1000 / (int64_t)(sizeof(float) * SF2_AUDIO_RATE));
}

/* ---- vídeo --------------------------------------------------------------------------------------------------- */
static void get_dst_rect(const App *a, SDL_FRect *dst) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(a->ren, &w, &h);
    /* la altura no depende del ancho del cuadro: con 4:3 la pantalla de la recreativa mide 384x288 y cada píxel de más
     * a los lados añade ancho (512 da 16:9), así que la pantalla ancha no estira nada */
    float aw = (float)sf2_frame_width(a->sf2), ah = a->cfg.aspect43 ? SF2_SCREEN_W * 3.0f / 4.0f : SF2_SCREEN_H;
    float s = fminf((float)w / aw, (float)h / ah);
    if (a->cfg.integer_scale && !a->cfg.aspect43 && s >= 1.0f)
        s = floorf(s);
    dst->w = aw * s;
    dst->h = ah * s;
    dst->x = ((float)w - dst->w) / 2;
    dst->y = ((float)h - dst->h) / 2;
}

/* HFR: el cuadro que se enseña va un cuadro del juego por detrás del último emulado: en el instante en que llega el cuadro N se enseña el N-1, y t sube de 0
 * a 256 a lo largo de un periodo de cuadro hasta enseñar el N justo cuando llega el N+1.  Con el juego parado, en avance rápido o con el mod apagado se
 * enseña el cuadro normal. */
static void read_refresh(App *a) {
    a->refresh_hz = 0;
    if (!a->win) return;
    const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(a->win));
    if (dm) a->refresh_hz = (double)dm->refresh_rate;
}

static void hfr_update(App *a, uint64_t now) {
    if (!a->cfg.hfr || a->bench)
        return;
    int t = 256;
    /* monitor de 60 Hz (o presentación que en la práctica va a 60): no hay cuadros que intercalar, ni latencia de más */
    const double hz = a->measured_hz > 0 ? a->measured_hz : a->refresh_hz;
    if (hz > 0 && hz < 1.5 * SF2_FRAME_HZ) t = 256;
    else if (a->paused || a->halted) t = a->hfr_t < 0 ? 256 : a->hfr_t;
    else if (!a->fast && a->frame_arrive_ns) {
        uint64_t d = now > a->frame_arrive_ns ? now - a->frame_arrive_ns : 0;
        t = d >= a->frame_ns ? 256 : (int)(d * 256 / a->frame_ns);
    }
    if (a->fast) {
        a->hfr_t = -1;
        return;                         /* el cuadro que dejó run_one_frame ya es el normal */
    }
    if (a->new_frame || t != a->hfr_t) {
        sf2_render_interp(a->sf2, t);
        a->new_frame = 1;
        a->hfr_t = t;
        a->hfr_draws++;
        a->hfr_hist[t >> 5 > 7 ? 7 : t >> 5]++;
    }
}

static void menu_draw(App *a);
static void draw_inputs(App *a, const SDL_FRect *dst);
static void draw_hitboxes(App *a, const SDL_FRect *dst);
static int apply_training(App *a);

static void draw(App *a) {
    hfr_update(a, SDL_GetTicksNS());
    a->stat_draws++;
    if (a->new_frame) {
        SDL_UpdateTexture(a->tex, NULL, sf2_frame(a->sf2), sf2_frame_width(a->sf2) * 4);
        a->new_frame = 0;
    }
    SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 255);
    SDL_RenderClear(a->ren);
    SDL_FRect dst;
    get_dst_rect(a, &dst);
    SDL_RenderTexture(a->ren, a->tex, NULL, &dst);
    uint64_t now = SDL_GetTicksNS();
    if (a->osd[0] && now < a->osd_until_ns) {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(a->ren, &w, &h);
        float k = w >= 900 ? 2.0f : 1.0f;
        SDL_SetRenderScale(a->ren, k, k);
        SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 170);
        SDL_FRect box = { 4, 4, (float)strlen(a->osd) * 8 + 8, 16 };
        SDL_RenderFillRect(a->ren, &box);
        SDL_SetRenderDrawColor(a->ren, 255, 255, 255, 255);
        SDL_RenderDebugText(a->ren, 8, 8, a->osd);
        SDL_SetRenderScale(a->ren, 1.0f, 1.0f);
    }
    if (a->cfg.show_hitboxes) draw_hitboxes(a, &dst);
    if (a->cfg.show_inputs) draw_inputs(a, &dst);
    if (a->shot_png && a->frames_run >= a->shot_frame) {
        SDL_Surface *surf = SDL_RenderReadPixels(a->ren, NULL);
        SDL_Surface *conv = surf ? SDL_ConvertSurface(surf, SDL_PIXELFORMAT_XRGB8888) : NULL;
        if (conv) {
            hostio_write_png(a->shot_png, (const uint32_t *)conv->pixels, conv->w, conv->h);
            fprintf(stderr, "captura de la ventana en %s (cuadro %llu)\n", a->shot_png, (unsigned long long)a->frames_run);
        }
        SDL_DestroySurface(conv);
        SDL_DestroySurface(surf);
        a->shot_png = NULL;
        SDL_Event q;
        SDL_zero(q);
        q.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&q);
    }
    if (a->menu) menu_draw(a);
    if (a->menu && a->menu_png && !a->menu_shot_done) {
        SDL_Surface *surf = SDL_RenderReadPixels(a->ren, NULL);
        SDL_Surface *conv = surf ? SDL_ConvertSurface(surf, SDL_PIXELFORMAT_XRGB8888) : NULL;
        if (conv) {
            hostio_write_png(a->menu_png, (const uint32_t *)conv->pixels, conv->w, conv->h);
            fprintf(stderr, "menu guardado en %s\n", a->menu_png);
        }
        SDL_DestroySurface(conv);
        SDL_DestroySurface(surf);
        a->menu_shot_done = 1;
    }
    uint64_t p0 = SDL_GetTicksNS();
    SDL_RenderPresent(a->ren);
    a->present_ns += SDL_GetTicksNS() - p0;
    a->present_n++;
}

static void write_screenshot(App *a) {
    char path[1300];
    snprintf(path, sizeof path, "%s/sf2_%llu.png", a->shot_dir, (unsigned long long)sf2_frame_count(a->sf2));
    SDL_CreateDirectory(a->shot_dir);
    if (hostio_write_png(path, sf2_frame(a->sf2), sf2_frame_width(a->sf2), SF2_SCREEN_H) == 0) {
        persist_flush();
        osd(a, "Captura guardada en %s", path);
    } else {
        osd(a, "No se pudo guardar la captura en %s", path);
    }
}

/* ---- un cuadro --------------------------------------------------------------------------------------------- */
static int parse_frame_list(const char *s, uint64_t **out) {
    int n = 0, cap = 0;
    *out = NULL;
    while (*s) {
        char *end;
        unsigned long long v = strtoull(s, &end, 10);
        if (end == s) return -1;
        if (n == cap) { cap = cap ? cap * 2 : 16; *out = realloc(*out, (size_t)cap * sizeof **out); }
        (*out)[n++] = v;
        s = end;
        while (*s == ',' || *s == ' ') s++;
    }
    return n;
}

/* captura de cuadros concretos (--png-frames): se escribe la imagen del punto de muestra de ese cuadro */
static void on_sample(void *user, uint64_t frame, const uint32_t *rgb) {
    App *a = user;
    for (int i = 0; i < a->npng; i++)
        if (a->png_frames[i] == frame) {
            char path[1300];
            snprintf(path, sizeof path, "%s/cuadro_%llu.png", a->png_dir, (unsigned long long)frame);
            if (hostio_write_png(path, rgb, sf2_frame_width(a->sf2), SF2_SCREEN_H) != 0)
                fprintf(stderr, "no se pudo escribir %s\n", path);
        }
}

/* ---- registro de fallos ------------------------------------------------------------------------------------- */
static void rescue_keep(App *a) {
    size_t n = sf2_state_size(a->sf2);
    if (n > a->rescue_cap) {
        uint8_t *b = realloc(a->rescue, n);
        if (!b)
            return;
        a->rescue = b;
        a->rescue_cap = n;
    }
    int w = sf2_state_save(a->sf2, a->rescue, a->rescue_cap);
    a->rescue_len = w > 0 ? (size_t)w : 0;
    a->rescue_frame = sf2_frame_count(a->sf2);
}

/* El juego se detuvo: el motivo va a stderr, a la pantalla y a errores.log en la carpeta de preferencias, con el
 * último estado bueno (como mucho RESCUE_EVERY cuadros antes) para reproducirlo con --load-state.  Nunca en silencio. */
static void report_failure(App *a) {
    char log_path[1200], st_path[1200], when[64];
    uint32_t from = 0, to = 0;
    int dispatch = sf2_dispatch_failure(a->sf2, &from, &to);
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (!tmv || !strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tmv))
        snprintf(when, sizeof when, "?");
    fprintf(stderr, "El juego se detuvo: %s\n", sf2_error(a->sf2));
    st_path[0] = 0;
    if (a->rescue_len) {
        SDL_CreateDirectory(a->state_dir);
        snprintf(st_path, sizeof st_path, "%s/fallo_cuadro%llu.sta", a->state_dir, (unsigned long long)a->rescue_frame);
        if (hostio_write_file(st_path, a->rescue, a->rescue_len) != 0)
            st_path[0] = 0;
    }
    snprintf(log_path, sizeof log_path, "%s/errores.log", a->pref_dir);
    SDL_CreateDirectory(a->pref_dir);
    FILE *f = fopen(log_path, "a");
    if (f) {
        fprintf(f, "[%s] %s\n", when, sf2_error(a->sf2));
        if (dispatch)
            fprintf(f, "  salto sin codigo recompilado: destino %06x desde la instruccion %06x; remedio: 'entry %06x' en "
                       "tools/hints.txt y recompilar\n", to, from, to);
        if (st_path[0])
            fprintf(f, "  estado del cuadro %llu (ultimo guardado antes del fallo): %s  (sf2 --load-state para reproducirlo)\n",
                    (unsigned long long)a->rescue_frame, st_path);
        fclose(f);
        fprintf(stderr, "informe en %s%s%s\n", log_path, st_path[0] ? ", estado para reproducirlo en " : "", st_path);
    }
    persist_flush();
    if (dispatch)
        osd(a, "Fallo: salto a %06x sin codigo recompilado (ver errores.log; F10 reinicia, F7 carga)", to);
    else
        osd(a, "Error en el codigo recompilado (ver errores.log; F10 reinicia, F7 carga un estado)");
}

/* devuelve 0 si sigue, 1 si hay que terminar */
static int run_one_frame(App *a, int push_audio) {
    uint16_t in0 = 0, in1 = 0, in2 = 0;
    if (!a->bench)
        input_poll(a->input, &in0, &in1, &in2);
    sf2_set_input(a->sf2, in0, in1, in2);
    if (a->debug_jump_frame && sf2_frame_count(a->sf2) == a->debug_jump_frame) {
        sf2_debug_jump(a->sf2, a->debug_jump_pc);
        a->debug_jump_frame = 0;
    }
    int vb = sf2_run_frame(a->sf2);
    if (vb > 1) a->multi_vblank_runs++;
    if (vb > a->max_vblanks_run) a->max_vblanks_run = vb;
    if (vb < 0) {
        a->halted = 1;
        report_failure(a);
        return 0;
    }
    a->frames_run += (uint64_t)vb;
    uint64_t fc = sf2_frame_count(a->sf2);
    if (!a->bench && (fc < a->rescue_frame || fc >= a->rescue_frame + RESCUE_EVERY))
        rescue_keep(a);                 /* también tras cargar un estado o reiniciar (el cuadro va hacia atrás) */
    a->stat_frames += (uint64_t)vb;
    a->new_frame = 1;
    a->frame_arrive_ns = SDL_GetTicksNS();
    size_t n;
    const float *samples = sf2_audio(a->sf2, &n);
    if (a->audio && push_audio && n)
        SDL_PutAudioStreamData(a->audio, samples, (int)(n * sizeof(float)));
    sf2_audio_clear(a->sf2);
    return a->max_frames && sf2_frame_count(a->sf2) >= a->max_frames;
}

static void update_title(App *a, uint64_t now) {
    if (now - a->stat_t0_ns < 1000000000ull)
        return;
    a->fps = (double)a->stat_frames * 1e9 / (double)(now - a->stat_t0_ns);
    const double hz = (double)a->stat_draws * 1e9 / (double)(now - a->stat_t0_ns);
    a->measured_hz = hz;
    if (a->cfg.hfr && a->cfg.vsync && !a->soft_pace && a->ren && !a->paused && !a->halted && !a->fast && a->refresh_hz >= 90 && hz > 0 && hz < 1.5 * SF2_FRAME_HZ) {
        /* con vsync la presentación va a ~60 Hz aunque el monitor sea de más: se sigue su refresco con un reloj propio (sin vsync) */
        a->soft_pace = 1;
        SDL_SetRenderVSync(a->ren, 0);
        a->measured_hz = 0;
        osd(a, "HFR: la presentacion iba a %.0f Hz con vsync; se sigue el refresco del monitor (%.0f Hz) con reloj propio", hz, a->refresh_hz);
    }
    a->stat_t0_ns = now;
    a->stat_frames = 0;
    a->stat_draws = 0;
    read_refresh(a);
    if (a->win) {
        char title[160];
        char hfr[48] = "";
        if (a->cfg.hfr) snprintf(hfr, sizeof hfr, " (HFR %.0f Hz)", hz);
        snprintf(title, sizeof title, APP_NAME " - %.1f fps%s%s%s - ranura %d", a->fps, hfr, a->paused ? " - PAUSA" : "",
                 a->fast ? " - rapido" : "", a->slot);
        SDL_SetWindowTitle(a->win, title);
    }
}

/* ---- medidas -------------------------------------------------------------------------------------------------- */
static const char *bench_name(BenchMode m) {
    return m == BENCH_CORE ? "core" : m == BENCH_VIDEO ? "video" : "full";
}

static SDL_AppResult bench_step(App *a) {
    uint64_t t0 = SDL_GetTicksNS();
    uint64_t budget = a->bench == BENCH_FULL ? FF_BUDGET_NS : 50000000ull;
    if (!a->bench_t0)
        a->bench_t0 = SDL_GetTicksNS();
    do {
        if (run_one_frame(a, 0) || a->halted)
            break;
        a->bench_frames++;
        if (a->bench_frames >= a->bench_target)
            break;
    } while (SDL_GetTicksNS() - t0 < budget);
    if (a->bench == BENCH_FULL)
        draw(a);
    if (a->halted || a->bench_frames >= a->bench_target) {
        double s = (double)(SDL_GetTicksNS() - a->bench_t0) / 1e9;
        double fps = (double)a->bench_frames / s;
        printf("bench modo=%s cuadros=%llu tiempo=%.3f s fps=%.1f (%.1fx tiempo real)\n", bench_name(a->bench),
               (unsigned long long)a->bench_frames, s, fps, fps / SF2_FRAME_HZ);
        fflush(stdout);
        return a->halted ? SDL_APP_FAILURE : SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

/* ---- SDL_AppInit ------------------------------------------------------------------------------------------- */
static void apply_dips(App *a) {
    uint8_t da, db, dc;
    config_dsw(&a->cfg, &da, &db, &dc);
    sf2_set_dips(a->sf2, da, db, dc);
}

static void make_dirs(App *a) {
    const char *base = a->pref_dir;
    snprintf(a->state_dir, sizeof a->state_dir, "%s", a->cfg.state_dir[0] ? a->cfg.state_dir : "");
    if (!a->state_dir[0]) snprintf(a->state_dir, sizeof a->state_dir, "%sstates", base);
    snprintf(a->shot_dir, sizeof a->shot_dir, "%sscreenshots", base);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    App *a = &g_app;
    memset(a, 0, sizeof *a);
    *appstate = a;
    SDL_SetAppMetadata(APP_NAME, "0.5.0", "dev.sf2recomp");
    config_defaults(&a->cfg);
    a->frame_ns = (uint64_t)(1e9 / SF2_FRAME_HZ);

    const char *cfg_arg = NULL, *rom_arg = NULL, *inputs = NULL, *load_state_arg = NULL, *png_list = NULL;
    int opt_scale = -1, opt_fs = 0, opt_wide = 0, opt_hud = 0, opt_logic = 0, opt_stage = 0, opt_hfr = 0, opt_training = 0, opt_dummy = -1, opt_show_inputs = 0, opt_show_hitboxes = 0, opt_dev = 0, opt_vol = -1, opt_mute = 0, write_cfg = 0;
    const char *opt_dscript = NULL;
    a->png_dir = ".";
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (!strcmp(s, "--help") || !strcmp(s, "-h")) { usage(argv[0]); return SDL_APP_SUCCESS; }
        else if (!strcmp(s, "--config") && i + 1 < argc) cfg_arg = argv[++i];
        else if (!strcmp(s, "--rom") && i + 1 < argc) rom_arg = argv[++i];
        else if (!strcmp(s, "--scale") && i + 1 < argc) opt_scale = atoi(argv[++i]);
        else if (!strcmp(s, "--fullscreen")) opt_fs = 1;
        else if (!strcmp(s, "--widescreen")) opt_wide = 1;
        else if (!strcmp(s, "--hud-edges")) opt_hud = 1;
        else if (!strcmp(s, "--wide-logic")) opt_logic = 1;
        else if (!strcmp(s, "--stage-full")) opt_stage = 1;
        else if (!strcmp(s, "--hfr")) opt_hfr = 1;
        else if (!strcmp(s, "--training")) opt_training = 1;
        else if (!strcmp(s, "--dummy") && i + 1 < argc) {
            const char *v = argv[++i];
            int found = -1;
            for (int k = 0; k < SF2_CONFIG_DUMMY_MODES; k++) if (!strcmp(v, config_dummy_names[k])) found = k;
            if (found < 0) { fprintf(stderr, "--dummy: stand, crouch, jump, block, block_crouch, walk_forward, walk_back o script\n"); return SDL_APP_FAILURE; }
            opt_dummy = found;
        }
        else if (!strcmp(s, "--dummy-script") && i + 1 < argc) { opt_dscript = argv[++i]; opt_dummy = SF2_DUMMY_SCRIPT; }
        else if (!strcmp(s, "--shot-png") && i + 1 < argc) {
            const char *v = argv[++i], *c = strrchr(v, ':');
            if (!c) { fprintf(stderr, "--shot-png: se espera RUTA:CUADRO\n"); return SDL_APP_FAILURE; }
            static char shot_path[1024];
            snprintf(shot_path, sizeof shot_path, "%.*s", (int)(c - v), v);
            a->shot_png = shot_path;
            a->shot_frame = strtoull(c + 1, NULL, 0);
        }
        else if (!strcmp(s, "--show-inputs")) opt_show_inputs = 1;
        else if (!strcmp(s, "--show-hitboxes")) opt_show_hitboxes = 1;
        else if (!strcmp(s, "--dev-tool") && i + 1 < argc) opt_dev = atoi(argv[++i]);
        else if (!strcmp(s, "--menu-png") && i + 1 < argc) a->menu_png = argv[++i];
        else if (!strcmp(s, "--menu-test")) a->menu_test = 1;
        else if (!strcmp(s, "--menu-page") && i + 1 < argc) a->menu_shot_page = atoi(argv[++i]);
        else if (!strcmp(s, "--volume") && i + 1 < argc) opt_vol = atoi(argv[++i]);
        else if (!strcmp(s, "--mute")) opt_mute = 1;
        else if (!strcmp(s, "--nothrottle")) a->nothrottle = 1;
        else if (!strcmp(s, "--inputs") && i + 1 < argc) inputs = argv[++i];
        else if (!strcmp(s, "--frames") && i + 1 < argc) a->max_frames = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(s, "--load-state") && i + 1 < argc) load_state_arg = argv[++i];
        else if (!strcmp(s, "--debug-jump") && i + 1 < argc) {
            unsigned long long fr = 0;
            unsigned pc = 0;
            if (sscanf(argv[++i], "%llu:%x", &fr, &pc) != 2 || !fr) { fprintf(stderr, "--debug-jump: se espera CUADRO:DIRECCION\n"); return SDL_APP_FAILURE; }
            a->debug_jump_frame = fr;
            a->debug_jump_pc = pc;
        }
        else if (!strcmp(s, "--png-frames") && i + 1 < argc) png_list = argv[++i];
        else if (!strcmp(s, "--png-dir") && i + 1 < argc) a->png_dir = argv[++i];
        else if (!strcmp(s, "--bench") && i + 1 < argc) { a->bench_target = strtoull(argv[++i], NULL, 0); if (!a->bench) a->bench = BENCH_FULL; }
        else if (!strcmp(s, "--bench-mode") && i + 1 < argc) {
            const char *m = argv[++i];
            a->bench = !strcmp(m, "core") ? BENCH_CORE : !strcmp(m, "video") ? BENCH_VIDEO : BENCH_FULL;
            if (!a->bench_target) a->bench_target = 3000;
        }
        else if (!strcmp(s, "--write-config")) write_cfg = 1;
        else if (!strcmp(s, "--stats")) a->stats = 1;
        else if (!strcmp(s, "--quiet")) a->quiet = 1;
        else if (s[0] == '-') { fprintf(stderr, "opcion desconocida: %s\n", s); usage(argv[0]); return SDL_APP_FAILURE; }
        else rom_arg = s;
    }

    /* carpetas y configuracion */
#ifdef __EMSCRIPTEN__
    snprintf(a->pref_dir, sizeof a->pref_dir, "/persist/");
#else
    if (cfg_arg) {
        /* con --config el perfil es la carpeta del archivo: estados y capturas van a su lado y no se toca la de preferencias */
        const char *slash = cfg_arg, *p = cfg_arg;
        for (; *p; p++) if (*p == '/' || *p == '\\') slash = p + 1;
        snprintf(a->pref_dir, sizeof a->pref_dir, "%.*s", (int)(slash - cfg_arg), cfg_arg);
        if (!a->pref_dir[0]) snprintf(a->pref_dir, sizeof a->pref_dir, "./");
    } else {
        char *pref = SDL_GetPrefPath("SF2Recomp", "sf2recomp");
        snprintf(a->pref_dir, sizeof a->pref_dir, "%s", pref ? pref : "./");
        SDL_free(pref);
    }
#endif
    snprintf(a->cfg_path, sizeof a->cfg_path, "%s", cfg_arg ? cfg_arg : "");
    if (!a->cfg_path[0]) snprintf(a->cfg_path, sizeof a->cfg_path, "%ssf2recomp.ini", a->pref_dir);
    uint8_t *text;
    size_t tlen;
    if (hostio_read_file(a->cfg_path, &text, &tlen) == 0) {
        char warn[1024];
        int w = config_parse(&a->cfg, (const char *)text, warn, sizeof warn);
        if (w) fprintf(stderr, "configuracion (%s): %d aviso(s)\n%s", a->cfg_path, w, warn);
        free(text);
    } else {
        /* primera vez (o archivo ausente): se escribe la configuracion por omision para que se pueda editar */
        char *out = malloc(16384);
        size_t n = out ? config_format(&a->cfg, out, 16384) : 0;
        if (n && n < 16384 && hostio_write_file(a->cfg_path, out, n) == 0) {
            persist_flush();
            if (!a->quiet) fprintf(stderr, "configuracion creada en %s\n", a->cfg_path);
        }
        free(out);
    }
    if (opt_scale >= 0) a->cfg.scale = opt_scale;
    if (opt_fs) a->cfg.fullscreen = 1;
    if (opt_wide) a->cfg.widescreen = 1;
    if (opt_hud) a->cfg.hud_edges = 1;
    if (opt_logic) a->cfg.wide_logic = 1;
    if (opt_stage) { a->cfg.stage_full = 1; a->cfg.widescreen = 1; }
    if (opt_hfr) a->cfg.hfr = 1;
    if (opt_training) { a->cfg.tr_enabled = 1; a->cfg.show_inputs = 1; }
    if (opt_dummy >= 0) a->cfg.tr_dummy = opt_dummy;
    if (opt_dscript) snprintf(a->cfg.tr_script, sizeof a->cfg.tr_script, "%s", opt_dscript);
    if (opt_show_inputs) a->cfg.show_inputs = 1;
    if (opt_show_hitboxes) a->cfg.show_hitboxes = 1;
    a->hfr_t = -1;
    a->capture = -1;
    if (opt_vol >= 0) a->cfg.volume = opt_vol > 100 ? 100 : opt_vol;
    if (opt_mute) a->cfg.audio = 0;
    if (write_cfg) {
        char *out = malloc(16384);
        size_t n = out ? config_format(&a->cfg, out, 16384) : 0;
        int rc = n && n < 16384 ? hostio_write_file(a->cfg_path, out, n) : -1;
        fprintf(stderr, rc == 0 ? "configuracion escrita en %s\n" : "no se pudo escribir %s\n", a->cfg_path);
        free(out);
        return rc == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    make_dirs(a);
    if (png_list && (a->npng = parse_frame_list(png_list, &a->png_frames)) < 0) {
        fprintf(stderr, "--png-frames: lista de cuadros invalida\n");
        return SDL_APP_FAILURE;
    }

    /* ROMs y maquina */
    int needs_window = a->bench == BENCH_OFF || a->bench == BENCH_FULL;
    if (!SDL_Init(a->bench == BENCH_CORE ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    RomSet rs;
    if (load_roms(a, rom_arg, &rs) != 0)
        return SDL_APP_FAILURE;
    Sf2Options opt;
    sf2_options_default(&opt);
    config_dsw(&a->cfg, &opt.dswa, &opt.dswb, &opt.dswc);
    opt.video = a->bench != BENCH_CORE;
    opt.sound = a->bench == BENCH_OFF || a->bench == BENCH_FULL;
    char err[600];
    if (sf2_create(&a->sf2, &rs, &opt, err, sizeof err) != 0) {
        romset_free(&rs);
        fatal_message(APP_NAME, err);
        return SDL_APP_FAILURE;
    }
    romset_free(&rs);
    if (a->cfg.widescreen && sf2_set_video_width(a->sf2, a->cfg.wide_width) != 0)
        fprintf(stderr, "wide_width = %d no es valido (par, de 384 a 512): pantalla ancha apagada\n", a->cfg.wide_width);
    sf2_set_hud_edges(a->sf2, a->cfg.hud_edges);
    sf2_set_wide_logic(a->sf2, a->cfg.wide_logic);
    sf2_set_stage_full(a->sf2, a->cfg.stage_full);
    apply_training(a);
    if (a->npng)
        sf2_set_sample_hook(a->sf2, on_sample, a);
    if (inputs) {
        uint8_t *script;
        size_t slen;
        if (hostio_read_file(inputs, &script, &slen) != 0) {
            fprintf(stderr, "no se pudo leer la secuencia de entradas %s\n", inputs);
            return SDL_APP_FAILURE;
        }
        int n = sf2_load_input_script(a->sf2, (const char *)script, inputs, err, sizeof err);
        free(script);
        if (n < 0) { fprintf(stderr, "%s", err); return SDL_APP_FAILURE; }
        if (!a->quiet) fprintf(stderr, "entradas: %d eventos de %s\n", n, inputs);
    }
    if (opt_dev > 0 && opt_dev <= 2) {
        a->dev_sel = opt_dev;
        sf2_set_dev_tool(a->sf2, opt_dev);
    }
    if (load_state_arg && load_state_file(a, load_state_arg) != 0)
        return SDL_APP_FAILURE;

    /* ventana, entradas y audio */
    if (needs_window) {
        int w = (a->cfg.scale ? a->cfg.scale : 3) * sf2_frame_width(a->sf2);
        int h = (a->cfg.scale ? a->cfg.scale : 3) * (a->cfg.aspect43 ? SF2_SCREEN_W * 3 / 4 : SF2_SCREEN_H);
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (a->cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
        if (!SDL_CreateWindowAndRenderer(APP_NAME, w, h, flags, &a->win, &a->ren)) {
            fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        SDL_SetRenderVSync(a->ren, a->bench == BENCH_FULL || a->nothrottle || !a->cfg.vsync ? 0 : 1);
        a->tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, sf2_frame_width(a->sf2), SF2_SCREEN_H);
        if (!a->tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return SDL_APP_FAILURE; }
        SDL_SetTextureScaleMode(a->tex, a->cfg.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        a->new_frame = 1;
        a->input = input_create(&a->cfg);
        input_open_existing(a->input);
    } else {
        a->input = input_create(&a->cfg);
    }
    if (a->bench == BENCH_OFF || a->bench == BENCH_FULL)
        audio_open(a);
    a->stat_t0_ns = SDL_GetTicksNS();
    if (!a->quiet)
        fprintf(stderr, "%s: %s, %s, %s\n", APP_NAME, a->win ? "ventana abierta" : "sin ventana",
                a->audio ? "con sonido (ritmo por el audio)" : "sin sonido (ritmo por reloj)",
                a->bench ? "modo de medida" : "listo");
    return SDL_APP_CONTINUE;
}

/* ---- SDL_AppEvent -------------------------------------------------------------------------------------------- */
/* activa o apaga la pantalla ancha en caliente: cambia el ancho del cuadro, rehace la textura y ajusta el ancho de la ventana */
/* cambia el ancho del cuadro (384 = pantalla ancha apagada) y rehace la textura y el tamaño de la ventana */
static int apply_width(App *a, int new_w) {
    int old_w = sf2_frame_width(a->sf2);
    if (new_w == old_w)
        return 0;
    if (sf2_set_video_width(a->sf2, new_w) != 0) {
        osd(a, "Ancho de pantalla no valido (%d)", new_w);
        return -1;
    }
    a->cfg.widescreen = new_w > SF2_SCREEN_W;
    if (a->win && a->ren) {
        if (a->tex) SDL_DestroyTexture(a->tex);
        a->tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, new_w, SF2_SCREEN_H);
        if (a->tex)
            SDL_SetTextureScaleMode(a->tex, a->cfg.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
#ifndef __EMSCRIPTEN__
        if (!(SDL_GetWindowFlags(a->win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(a->win, &ww, &wh);
            SDL_SetWindowSize(a->win, ww * new_w / old_w, wh);
        }
#endif
    }
    a->new_frame = 1;
    return 0;
}

static void set_widescreen(App *a, int on) {
    int new_w = on ? a->cfg.wide_width : SF2_SCREEN_W;
    if (on && new_w <= SF2_SCREEN_W) new_w = 512;
    if (apply_width(a, new_w) != 0) return;
    a->cfg.widescreen = on;
    if (on) osd(a, "Pantalla ancha: %dx%d", sf2_frame_width(a->sf2), SF2_SCREEN_H);
    else osd(a, "Pantalla ancha apagada");
}

static void toggle_widescreen(App *a) { set_widescreen(a, sf2_frame_width(a->sf2) <= SF2_SCREEN_W); }

static void set_stage_full(App *a, int on) {
    a->cfg.stage_full = on;
    sf2_set_stage_full(a->sf2, on);
    osd(a, on ? "Stage completo: activado" : "Stage completo: apagado");
}

static void set_wide_logic(App *a, int on) {
    a->cfg.wide_logic = on;
    sf2_set_wide_logic(a->sf2, on);
    osd(a, on ? (sf2_frame_width(a->sf2) > SF2_SCREEN_W ? "Logica ancha: activada" : "Logica ancha: activada (necesita la pantalla ancha, F9)") : "Logica ancha: juego original");
}

static void set_hfr(App *a, int on) {
    a->cfg.hfr = on;
    a->hfr_t = -1;
    if (!on) {                                                                      /* vuelve al cuadro normal */
        sf2_render_interp(a->sf2, 256);
        a->new_frame = 1;
        if (a->soft_pace) { a->soft_pace = 0; SDL_SetRenderVSync(a->ren, a->cfg.vsync ? 1 : 0); }
    }
    osd(a, on ? "HFR: activado" : "HFR: apagado");
}


/* ---- mod de entrenamiento (docs/ENTRENAMIENTO.md) ------------------------------------------------------------------------ */
static int apply_training(App *a) {
    Sf2Training t;
    memset(&t, 0, sizeof t);
    t.enabled = a->cfg.tr_enabled;
    t.infinite_time = a->cfg.tr_infinite_time;
    t.refill = a->cfg.tr_refill;
    t.refill_delay = a->cfg.tr_refill_delay;
    t.lock_p2 = a->cfg.tr_lock_p2;
    t.auto_vs = a->cfg.tr_auto_vs;
    t.dummy = a->cfg.tr_dummy;
    snprintf(t.script, sizeof t.script, "%s", a->cfg.tr_script);
    t.track_inputs = a->cfg.show_inputs;
    char err[200];
    if (sf2_training_set(a->sf2, &t, err, sizeof err) != 0) {
        /* guion no valido: el modo del dummy no puede ser script; se avisa y se queda como estaba */
        osd(a, "Entrenamiento: %s", err);
        a->cfg.tr_enabled = 0;
        t.enabled = 0;
        sf2_training_set(a->sf2, &t, NULL, 0);
        return -1;
    }
    return 0;
}

static void set_training(App *a, int on) {
    a->cfg.tr_enabled = on;
    if (apply_training(a) != 0)
        return;
    osd(a, on ? "Entrenamiento: activado (cambia el juego)" : "Entrenamiento: apagado (juego original)");
}

static void set_show_inputs(App *a, int on) {
    a->cfg.show_inputs = on;
    apply_training(a);
    osd(a, on ? "Visor de entradas: activado" : "Visor de entradas: apagado");
}

static void set_show_hitboxes(App *a, int on) {
    a->cfg.show_hitboxes = on;
    osd(a, on ? "Cajas de golpe: activadas" : "Cajas de golpe: apagadas");
}

/* direccion en notacion numerica (teclado numerico: 1-9) a partir de los bits SF2_PAD_* */
static char pad_digit(uint16_t m) {
    int dx = ((m & SF2_PAD_RIGHT) ? 1 : 0) - ((m & SF2_PAD_LEFT) ? 1 : 0);
    int dy = ((m & SF2_PAD_DOWN) ? 1 : 0) - ((m & SF2_PAD_UP) ? 1 : 0);
    return (char)('5' + dx - 3 * dy);
}

static void fill_rect(SDL_Renderer *r, float x, float y, float w, float h, Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
    SDL_FRect rc = { x, y, w, h };
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_RenderFillRect(r, &rc);
}

/* visor de entradas: palanca y seis botones en vivo y el historial de cada jugador (J1 a la izquierda, J2 a la derecha) */
static void draw_inputs(App *a, const SDL_FRect *dst) {
    static const struct { uint16_t bit; const char *name; } btn[6] = {
        { SF2_PAD_LP, "LP" }, { SF2_PAD_MP, "MP" }, { SF2_PAD_HP, "HP" }, { SF2_PAD_LK, "LK" }, { SF2_PAD_MK, "MK" }, { SF2_PAD_HK, "HK" } };
    const float k = dst->h >= 640 ? 2.0f : 1.0f, u = 6 * k, row = 9 * k;
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    for (int p = 0; p < 2; p++) {
        const uint16_t now = sf2_training_pad(a->sf2, p);
        const float x0 = p ? dst->x + dst->w - 4 - 62 * k : dst->x + 4;
        const float y0 = dst->y + dst->h - 4 - 3 * u - 2;
        /* historial, el mas reciente abajo (pegado a la palanca) */
        Sf2InputEvent ev[10];
        int n = sf2_training_history(a->sf2, p, ev, 10);
        fill_rect(a->ren, x0 - 2, y0 - (float)n * row - 4 * k, 62 * k, (float)n * row + 3 * u + 4 * k + 4, 0, 0, 0, 120);
        SDL_SetRenderScale(a->ren, k, k);
        char tag[8];
        snprintf(tag, sizeof tag, "P%d", p + 1);
        SDL_SetRenderDrawColor(a->ren, 255, 255, 255, 255);
        SDL_RenderDebugText(a->ren, (x0 + 62 * k - 18 * k) / k, (y0 + u) / k, tag);
        for (int i = 0; i < n; i++) {
            char line[40] = "", *w = line;
            w += snprintf(w, 8, "%c", pad_digit(ev[i].mask));
            for (int b = 0; b < 6; b++)
                if (ev[i].mask & btn[b].bit)
                    w += snprintf(w, 6, "%s%s", w == line + 1 ? " " : "+", btn[b].name);
            SDL_SetRenderDrawColor(a->ren, i == 0 ? 255 : 200, i == 0 ? 255 : 200, i == 0 ? 120 : 200, 255);
            SDL_RenderDebugText(a->ren, (x0 + 2) / k, (y0 - (float)(i + 1) * row) / k, line);
            char fr[8];
            snprintf(fr, sizeof fr, "%u", (unsigned)ev[i].frames);
            SDL_RenderDebugText(a->ren, (x0 + 62 * k - 2 - (float)strlen(fr) * 8 * k) / k, (y0 - (float)(i + 1) * row) / k, fr);
        }
        SDL_SetRenderScale(a->ren, 1.0f, 1.0f);
        /* palanca 3x3 */
        for (int cy = 0; cy < 3; cy++)
            for (int cx = 0; cx < 3; cx++) {
                /* una casilla se enciende si coincide exactamente con la direccion pulsada (diagonales incluidas) */
                const int dx = cx - 1, dy = cy - 1;
                const int pdx = ((now & SF2_PAD_RIGHT) ? 1 : 0) - ((now & SF2_PAD_LEFT) ? 1 : 0);
                const int pdy = ((now & SF2_PAD_DOWN) ? 1 : 0) - ((now & SF2_PAD_UP) ? 1 : 0);
                const int lit = !(cx == 1 && cy == 1) && dx == pdx && dy == pdy;
                fill_rect(a->ren, x0 + (float)cx * u, y0 + (float)cy * u, u - 1, u - 1, lit ? 255 : 70, lit ? 220 : 70, lit ? 60 : 80, 255);
            }
        /* botones: punos arriba, patadas abajo */
        for (int b = 0; b < 6; b++) {
            const int lit = (now & btn[b].bit) != 0;
            fill_rect(a->ren, x0 + 3 * u + 4 + (float)(b % 3) * (u + 2), y0 + (float)(b / 3) * (u + 2), u, u, lit ? (b < 3 ? 255 : 90) : 70, lit ? (b < 3 ? 90 : 200) : 70, lit ? 90 : 80, 255);
        }
    }
}

/* cajas de golpe y de daño (docs/HITBOXES.md): se piden al nucleo en coordenadas de 384x224 y se dibujan con la resolucion de la ventana;
 * con pantalla ancha el combate se centra, asi que se desplazan medio ancho extra (con stage_full el desplazamiento no es ese: sin resolver) */
static void draw_hitboxes(App *a, const SDL_FRect *dst) {
    static const Uint8 col[8][3] = {
        { 60, 140, 255 }, { 60, 140, 255 }, { 60, 140, 255 }, { 255, 230, 60 },   /* cabeza, cuerpo, pies, punto debil */
        { 255, 50, 50 }, { 60, 220, 90 },                                         /* ataque, empuje */
        { 255, 140, 40 }, { 170, 170, 170 } };                                    /* proyectil: ataque, empuje */
    Sf2Box box[SF2_MAX_BOXES];
    const int n = sf2_training_boxes(a->sf2, box, SF2_MAX_BOXES), fw = sf2_frame_width(a->sf2);
    if (n <= 0 || fw <= 0)
        return;
    const float kx = dst->w / (float)fw, ky = dst->h / (float)SF2_SCREEN_H, off = (float)(fw - SF2_SCREEN_W) / 2;
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < n; i++) {
        const Uint8 *c = col[box[i].kind & 7];
        SDL_FRect r = { dst->x + ((float)box[i].x + off) * kx, dst->y + (float)box[i].y * ky, (float)box[i].w * kx, (float)box[i].h * ky };
        const int attack = box[i].kind == SF2_BOX_ATTACK || box[i].kind == SF2_BOX_PROJ_ATTACK;
        SDL_SetRenderDrawColor(a->ren, c[0], c[1], c[2], attack ? 90 : 45);
        SDL_RenderFillRect(a->ren, &r);
        SDL_SetRenderDrawColor(a->ren, c[0], c[1], c[2], 255);
        SDL_RenderRect(a->ren, &r);
    }
}

static void set_hud(App *a, int edges) {
    a->cfg.hud_edges = edges;
    sf2_set_hud_edges(a->sf2, edges);
    a->new_frame = 1;
}

static void set_smooth(App *a, int on) {
    a->cfg.smooth = on;
    if (a->tex) SDL_SetTextureScaleMode(a->tex, on ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

static void set_volume(App *a, int v) {
    a->cfg.volume = v < 0 ? 0 : v > 100 ? 100 : v;
    if (a->audio) SDL_SetAudioStreamGain(a->audio, a->cfg.volume / 100.0f);
}

static void toggle_fullscreen(App *a) {
    bool fs = (SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(a->win, !fs);
}

/* ---- menú en el juego (tecla `menu`, por omisión M; en el mando, el botón guía) -------------------------------------------- */
typedef enum MenuPage { PG_MAIN, PG_MODS, PG_VIDEO, PG_AUDIO, PG_CONTROLS, PG_TRAINING, PG_BINDS, PG_COUNT } MenuPage;
enum {
    MI_RESUME, MI_PRESET, MI_SUB_MODS, MI_SUB_TRAINING, MI_SUB_VIDEO, MI_SUB_AUDIO, MI_SUB_CONTROLS, MI_SAVE, MI_RESET, MI_QUIT,
    MI_WIDE, MI_WIDTH, MI_HUD, MI_LOGIC, MI_STAGE, MI_HFR, MI_DEVTOOL,
    MI_SCALE, MI_FULLSCREEN, MI_INTSCALE, MI_ASPECT, MI_SMOOTH, MI_VSYNC,
    MI_SOUND, MI_VOLUME,
    MI_TR_ENABLED, MI_TR_TIME, MI_TR_REFILL, MI_TR_DELAY, MI_TR_LOCK, MI_TR_AUTOVS, MI_TR_DUMMY, MI_TR_INPUTS, MI_TR_HITBOXES,
    MI_KB1, MI_KB2, MI_PAD1, MI_PAD2, MI_RESTORE_ALL,
    MI_BIND, MI_RESTORE_PAGE, MI_BACK,
};

static const int page_main[] = { MI_RESUME, MI_PRESET, MI_SUB_MODS, MI_SUB_TRAINING, MI_SUB_VIDEO, MI_SUB_AUDIO, MI_SUB_CONTROLS, MI_SAVE, MI_RESET, MI_QUIT };
static const int page_mods[] = { MI_WIDE, MI_WIDTH, MI_HUD, MI_LOGIC, MI_STAGE, MI_HFR, MI_DEVTOOL, MI_BACK };
static const int page_video[] = { MI_SCALE, MI_FULLSCREEN, MI_INTSCALE, MI_ASPECT, MI_SMOOTH, MI_VSYNC, MI_BACK };
static const int page_audio[] = { MI_SOUND, MI_VOLUME, MI_BACK };
static const int page_training[] = { MI_TR_ENABLED, MI_TR_TIME, MI_TR_REFILL, MI_TR_DELAY, MI_TR_LOCK, MI_TR_AUTOVS, MI_TR_DUMMY, MI_TR_INPUTS, MI_TR_HITBOXES, MI_BACK };
static const int page_controls[] = { MI_KB1, MI_KB2, MI_PAD1, MI_PAD2, MI_RESTORE_ALL, MI_BACK };
static const struct { const int *items; int n; const char *title; } pages[PG_COUNT - 1] = {
    { page_main, (int)(sizeof page_main / sizeof *page_main), "" },
    { page_mods, (int)(sizeof page_mods / sizeof *page_mods), "Pantalla ancha y mods" },
    { page_video, (int)(sizeof page_video / sizeof *page_video), "Video" },
    { page_audio, (int)(sizeof page_audio / sizeof *page_audio), "Sonido" },
    { page_controls, (int)(sizeof page_controls / sizeof *page_controls), "Controles" },
    { page_training, (int)(sizeof page_training / sizeof *page_training), "Entrenamiento" },
};

/* controles de una página de asignaciones: la dirección, los seis botones, moneda, start y (solo el jugador 1) servicio */
#define N_BIND_ACTS 13
static int bind_action(const App *a, int idx) {
    const int base = a->bind_player ? SF2_ACT_P2_UP : SF2_ACT_P1_UP;
    if (idx < 10) return base + idx;
    if (idx == 10) return a->bind_player ? SF2_ACT_COIN2 : SF2_ACT_COIN1;
    if (idx == 11) return a->bind_player ? SF2_ACT_START2 : SF2_ACT_START1;
    return SF2_ACT_SERVICE;
}
static int bind_count(const App *a) { return a->bind_player ? N_BIND_ACTS - 1 : N_BIND_ACTS; }
static const char *const bind_labels[N_BIND_ACTS] = { "Arriba", "Abajo", "Izquierda", "Derecha", "Punetazo debil", "Punetazo medio", "Punetazo fuerte",
    "Patada debil", "Patada media", "Patada fuerte", "Moneda", "Start", "Servicio (test)" };

static int menu_count(const App *a) {
    if (a->menu_page == PG_BINDS) return bind_count(a) + 2;           /* + restaurar + volver */
    return pages[a->menu_page].n;
}

static int menu_id(const App *a, int idx) {
    if (a->menu_page == PG_BINDS) {
        const int n = bind_count(a);
        return idx < n ? MI_BIND : idx == n ? MI_RESTORE_PAGE : MI_BACK;
    }
    return pages[a->menu_page].items[idx];
}

static const char *const dev_tool_names[] = { "Apagado", "Scroll", "OBJ TEST (cajas)" };
#define N_DEV_TOOLS ((int)(sizeof dev_tool_names / sizeof dev_tool_names[0]))

typedef struct MenuPreset { const char *name; int wide, width, hud, logic, stage, aspect43; } MenuPreset;
static const MenuPreset presets[] = {
    { "Original",               0, 384, 0, 0, 0, 0 },
    { "Widescreen 16:9",        1, 512, 1, 0, 0, 1 },
    { "Ultraancho 3440x1440",   1, 688, 1, 0, 1, 1 },
    { "Ultraancho 768",         1, 768, 1, 0, 1, 1 },
};
#define N_PRESETS ((int)(sizeof presets / sizeof presets[0]))
static const int wide_widths[] = { 512, 576, 640, 688, 768, 800 };
#define N_WIDTHS ((int)(sizeof wide_widths / sizeof wide_widths[0]))

static int preset_matches(const App *a, const MenuPreset *p) {
    const int w = sf2_frame_width(a->sf2) > SF2_SCREEN_W;
    if (w != p->wide || a->cfg.stage_full != p->stage || a->cfg.wide_logic != p->logic)
        return 0;
    return !w || (a->cfg.wide_width == p->width && a->cfg.hud_edges == p->hud);
}

static int current_preset(const App *a) {
    for (int i = 0; i < N_PRESETS; i++)
        if (preset_matches(a, &presets[i])) return i;
    return -1;
}

/* tamaño de la ventana según la escala (0 = ajustar: se maximiza) */
static void apply_window_size(App *a) {
    if (!a->win) return;
    if (SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN) return;
    if (a->cfg.scale <= 0) { SDL_MaximizeWindow(a->win); return; }
    SDL_RestoreWindow(a->win);
    SDL_SetWindowSize(a->win, a->cfg.scale * sf2_frame_width(a->sf2), a->cfg.scale * (a->cfg.aspect43 ? SF2_SCREEN_W * 3 / 4 : SF2_SCREEN_H));
}

static void apply_preset(App *a, const MenuPreset *p) {
    a->cfg.wide_width = p->wide ? p->width : a->cfg.wide_width;
    if (p->wide) a->cfg.aspect43 = 1;                  /* con 4:3 la pantalla de la recreativa mide 384x288 y 512 sale 16:9 */
    set_hud(a, p->wide ? p->hud : a->cfg.hud_edges);
    if (!p->wide) { a->cfg.stage_full = 0; sf2_set_stage_full(a->sf2, 0); a->cfg.wide_logic = 0; sf2_set_wide_logic(a->sf2, 0); }
    set_widescreen(a, p->wide);
    if (p->wide) {
        a->cfg.stage_full = p->stage; sf2_set_stage_full(a->sf2, p->stage);
        a->cfg.wide_logic = p->logic; sf2_set_wide_logic(a->sf2, p->logic);
    }
    osd(a, "Ajuste: %s", p->name);
}

static int width_index(int w) {
    for (int i = 0; i < N_WIDTHS; i++)
        if (wide_widths[i] == w) return i;
    return 0;
}

static void save_config(App *a) {
    char *out = malloc(16384);
    size_t n = out ? config_format(&a->cfg, out, 16384) : 0;
    if (n && n < 16384 && hostio_write_file(a->cfg_path, out, n) == 0) {
        persist_flush();
        osd(a, "Configuracion guardada en %s", a->cfg_path);
    } else {
        osd(a, "No se pudo guardar la configuracion en %s", a->cfg_path);
    }
    free(out);
}

static void menu_close(App *a) {
    a->menu = 0;
    a->menu_page = PG_MAIN;
    a->capture = -1;
    a->next_ns = 0;
    a->new_frame = 1;
}

static void menu_goto(App *a, int page) {
    a->menu_sel_page[a->menu_page] = a->menu_sel;
    a->menu_page = page;
    a->menu_sel = a->menu_sel_page[page];
}

static void menu_back(App *a) {
    if (a->menu_page == PG_MAIN) { menu_close(a); return; }
    const int to = a->menu_page == PG_BINDS ? PG_CONTROLS : PG_MAIN;
    a->menu_sel_page[a->menu_page] = 0;
    a->menu_page = to;
    a->menu_sel = a->menu_sel_page[to];
}

static void restore_binds(App *a, int first, int count, int pad) {
    Sf2Config def;
    config_defaults(&def);
    for (int i = 0; i < count; i++) {
        const int act = first + i;
        snprintf(pad ? a->cfg.pads[act] : a->cfg.keys[act], SF2_BIND_LEN, "%s", pad ? def.pads[act] : def.keys[act]);
    }
}

/* dir: -1 izquierda, 0 aceptar, 1 derecha; devuelve 1 si hay que terminar */
static int menu_activate(App *a, int dir) {
    const int d = dir ? dir : 1, id = menu_id(a, a->menu_sel);
    switch (id) {
    case MI_RESUME: if (!dir) menu_close(a); break;
    case MI_PRESET: {
        int c = current_preset(a);
        c = c < 0 ? (d > 0 ? 0 : N_PRESETS - 1) : (c + d + N_PRESETS) % N_PRESETS;
        apply_preset(a, &presets[c]);
        break;
    }
    case MI_SUB_MODS: if (!dir) menu_goto(a, PG_MODS); break;
    case MI_SUB_TRAINING: if (!dir) menu_goto(a, PG_TRAINING); break;
    case MI_SUB_VIDEO: if (!dir) menu_goto(a, PG_VIDEO); break;
    case MI_SUB_AUDIO: if (!dir) menu_goto(a, PG_AUDIO); break;
    case MI_SUB_CONTROLS: if (!dir) menu_goto(a, PG_CONTROLS); break;
    case MI_SAVE: if (!dir) save_config(a); break;
    case MI_RESET: if (!dir) { sf2_reset(a->sf2); a->halted = 0; a->new_frame = 1; menu_close(a); osd(a, "Reinicio"); } break;
    case MI_QUIT: if (!dir) return 1; break;
    case MI_WIDE: set_widescreen(a, sf2_frame_width(a->sf2) <= SF2_SCREEN_W); break;
    case MI_WIDTH: {
        const int w = wide_widths[(width_index(a->cfg.wide_width) + d + N_WIDTHS) % N_WIDTHS];
        a->cfg.wide_width = w;
        if (sf2_frame_width(a->sf2) > SF2_SCREEN_W) apply_width(a, w);
        break;
    }
    case MI_HUD: set_hud(a, !a->cfg.hud_edges); break;
    case MI_LOGIC: set_wide_logic(a, !a->cfg.wide_logic); break;
    case MI_STAGE: set_stage_full(a, !a->cfg.stage_full); break;
    case MI_HFR: set_hfr(a, !a->cfg.hfr); break;
    case MI_DEVTOOL:
        if (dir) {
            a->dev_sel = (a->dev_sel + d + N_DEV_TOOLS) % N_DEV_TOOLS;
        } else {
            sf2_set_dev_tool(a->sf2, a->dev_sel);
            a->halted = 0;
            menu_close(a);
            osd(a, a->dev_sel ? "Herramienta de desarrollo: %s (teclas del jugador 1; F10 reinicia dentro de ella)" : "Herramientas de desarrollo apagadas: juego original", dev_tool_names[a->dev_sel]);
        }
        break;
    case MI_TR_ENABLED: set_training(a, !a->cfg.tr_enabled); break;
    case MI_TR_TIME: a->cfg.tr_infinite_time = !a->cfg.tr_infinite_time; apply_training(a); break;
    case MI_TR_REFILL: a->cfg.tr_refill = (a->cfg.tr_refill + d + 3) % 3; apply_training(a); break;
    case MI_TR_DELAY: {
        int v = a->cfg.tr_refill_delay + 10 * (dir ? dir : 1);
        a->cfg.tr_refill_delay = v < 10 ? 10 : v > 600 ? (dir ? 600 : 10) : v;
        apply_training(a);
        break;
    }
    case MI_TR_LOCK: a->cfg.tr_lock_p2 = !a->cfg.tr_lock_p2; apply_training(a); break;
    case MI_TR_AUTOVS: a->cfg.tr_auto_vs = !a->cfg.tr_auto_vs; apply_training(a); break;
    case MI_TR_DUMMY:
        a->cfg.tr_dummy = (a->cfg.tr_dummy + d + SF2_CONFIG_DUMMY_MODES) % SF2_CONFIG_DUMMY_MODES;
        apply_training(a);
        break;
    case MI_TR_INPUTS: a->cfg.show_inputs = !a->cfg.show_inputs; apply_training(a); break;
    case MI_TR_HITBOXES: a->cfg.show_hitboxes = !a->cfg.show_hitboxes; break;
    case MI_SCALE: a->cfg.scale = (a->cfg.scale + d + 7) % 7; apply_window_size(a); break;           /* 0 = ajustar, 1 a 6 */
    case MI_FULLSCREEN: if (a->win) toggle_fullscreen(a); break;
    case MI_INTSCALE: a->cfg.integer_scale = !a->cfg.integer_scale; break;
    case MI_ASPECT: a->cfg.aspect43 = !a->cfg.aspect43; apply_window_size(a); break;
    case MI_SMOOTH: set_smooth(a, !a->cfg.smooth); break;
    case MI_VSYNC:
        a->cfg.vsync = !a->cfg.vsync;
        a->soft_pace = 0;
        if (a->ren) SDL_SetRenderVSync(a->ren, a->cfg.vsync ? 1 : 0);
        osd(a, a->cfg.vsync ? "VSync: activado" : "VSync: apagado");
        break;
    case MI_SOUND:
        if (!a->audio) { osd(a, "Sonido no disponible en esta sesion"); break; }
        a->cfg.audio = !a->cfg.audio;
        SDL_SetAudioStreamGain(a->audio, a->cfg.audio ? a->cfg.volume / 100.0f : 0.0f);
        break;
    case MI_VOLUME: set_volume(a, dir ? a->cfg.volume + 10 * dir : (a->cfg.volume >= 100 ? 0 : a->cfg.volume + 10)); break;
    case MI_KB1: case MI_KB2: case MI_PAD1: case MI_PAD2:
        if (dir) break;
        a->bind_player = id == MI_KB2 || id == MI_PAD2;
        a->bind_pad = id == MI_PAD1 || id == MI_PAD2;
        menu_goto(a, PG_BINDS);
        a->menu_sel = 0;
        break;
    case MI_RESTORE_ALL:
        if (dir) break;
        restore_binds(a, 0, SF2_ACT_COUNT, 0);
        restore_binds(a, 0, SF2_ACT_COUNT, 1);
        input_reload(a->input, &a->cfg);
        osd(a, "Controles restaurados");
        break;
    case MI_BIND: if (!dir) a->capture = bind_action(a, a->menu_sel); break;
    case MI_RESTORE_PAGE:
        if (dir) break;
        for (int i = 0; i < bind_count(a); i++) restore_binds(a, bind_action(a, i), 1, a->bind_pad);
        input_reload(a->input, &a->cfg);
        osd(a, "Controles de esta pagina restaurados");
        break;
    case MI_BACK: if (!dir) menu_back(a); break;
    }
    return 0;
}

static void assign_bind(App *a, const char *name) {
    const int act = a->capture;
    a->capture = -1;
    if (a->bind_pad) snprintf(a->cfg.pads[act], SF2_BIND_LEN, "%s", name);
    else snprintf(a->cfg.keys[act], SF2_BIND_LEN, "%s", name);
    input_reload(a->input, &a->cfg);
    osd(a, "%s: %s", config_action_name(act), name[0] ? name : "(sin asignar)");
}

static int menu_key(App *a, SDL_Scancode sc) {
    if (a->capture >= 0) {
        if (sc == SDL_SCANCODE_ESCAPE) a->capture = -1;
        else if (sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_DELETE) assign_bind(a, "");
        else if (!a->bind_pad) assign_bind(a, SDL_GetScancodeName(sc));
        return 0;
    }
    const int n = menu_count(a);
    switch (sc) {
    case SDL_SCANCODE_UP: a->menu_sel = (a->menu_sel + n - 1) % n; break;
    case SDL_SCANCODE_DOWN: a->menu_sel = (a->menu_sel + 1) % n; break;
    case SDL_SCANCODE_LEFT: menu_activate(a, -1); break;
    case SDL_SCANCODE_RIGHT: menu_activate(a, 1); break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return menu_activate(a, 0);
    case SDL_SCANCODE_ESCAPE: menu_back(a); break;
    default: if (input_hotkey_matches(a->input, SF2_HK_MENU, sc)) menu_close(a); break;
    }
    return 0;
}

static int menu_pad(App *a, SDL_GamepadButton b) {
    if (a->capture >= 0) {
        if (a->bind_pad) assign_bind(a, SDL_GetGamepadStringForButton(b));
        return 0;
    }
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return menu_key(a, SDL_SCANCODE_UP);
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return menu_key(a, SDL_SCANCODE_DOWN);
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return menu_key(a, SDL_SCANCODE_LEFT);
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return menu_key(a, SDL_SCANCODE_RIGHT);
    case SDL_GAMEPAD_BUTTON_SOUTH: return menu_key(a, SDL_SCANCODE_RETURN);
    case SDL_GAMEPAD_BUTTON_EAST: menu_back(a); break;
    case SDL_GAMEPAD_BUTTON_START: case SDL_GAMEPAD_BUTTON_GUIDE: menu_close(a); break;
    default: break;
    }
    return 0;
}

/* los gatillos son ejes: al asignar, cuenta el primero que pasa de la mitad */
static void menu_axis(App *a, int axis, int value) {
    if (a->capture < 0 || !a->bind_pad || value < 16000) return;
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) assign_bind(a, "lefttrigger");
    else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) assign_bind(a, "righttrigger");
}

static const char *yesno(int v) { return v ? "si" : "no"; }

static void menu_item(const App *a, int idx, const char **label, char *val, size_t n, const char **hint) {
    static const char *const labels[] = {
        [MI_RESUME] = "Continuar", [MI_PRESET] = "Ajuste rapido", [MI_SUB_MODS] = "Pantalla ancha y mods  >", [MI_SUB_TRAINING] = "Entrenamiento  >", [MI_SUB_VIDEO] = "Video  >",
        [MI_SUB_AUDIO] = "Sonido  >", [MI_SUB_CONTROLS] = "Controles  >", [MI_SAVE] = "Guardar configuracion", [MI_RESET] = "Reiniciar el juego", [MI_QUIT] = "Salir",
        [MI_WIDE] = "Pantalla ancha (F9)", [MI_WIDTH] = "Ancho del cuadro", [MI_HUD] = "Marcador (HUD)", [MI_LOGIC] = "Logica ancha (F3)",
        [MI_STAGE] = "Stage completo (F4)", [MI_HFR] = "HFR (F1)", [MI_DEVTOOL] = "Herramientas de desarrollo",
        [MI_SCALE] = "Escala de la ventana", [MI_FULLSCREEN] = "Pantalla completa (F11)", [MI_INTSCALE] = "Escala entera",
        [MI_ASPECT] = "Relacion de aspecto", [MI_SMOOTH] = "Filtro suave", [MI_VSYNC] = "VSync",
        [MI_SOUND] = "Sonido", [MI_VOLUME] = "Volumen",
        [MI_TR_ENABLED] = "Modo entrenamiento (7)", [MI_TR_TIME] = "Sin limite de tiempo", [MI_TR_REFILL] = "Barras de vida", [MI_TR_DELAY] = "Retardo de la recuperacion",
        [MI_TR_LOCK] = "Jugador 2 sin controles", [MI_TR_AUTOVS] = "Versus automatico", [MI_TR_DUMMY] = "Comportamiento de J2",
        [MI_TR_INPUTS] = "Visor de entradas (8)", [MI_TR_HITBOXES] = "Cajas de golpe (9)",
        [MI_KB1] = "Teclado jugador 1  >", [MI_KB2] = "Teclado jugador 2  >", [MI_PAD1] = "Mando jugador 1  >", [MI_PAD2] = "Mando jugador 2  >",
        [MI_RESTORE_ALL] = "Restaurar todos los controles", [MI_RESTORE_PAGE] = "Restaurar esta pagina", [MI_BACK] = "< Volver",
    };
    static const char *const hints[] = {
        [MI_RESUME] = "Vuelve al juego (Esc, M o B).",
        [MI_PRESET] = "Original, widescreen 16:9 o ultraancho: ajusta varios mods a la vez.",
        [MI_SUB_MODS] = "Pantalla ancha, ancho, marcador, logica ancha, stage completo, HFR y herramientas de desarrollo.",
        [MI_SUB_TRAINING] = "Sin tiempo, barras, dummy con guion, entradas y cajas.",
        [MI_SUB_VIDEO] = "Escala, pantalla completa, aspecto, filtro y VSync.",
        [MI_SUB_AUDIO] = "Sonido y volumen.",
        [MI_SUB_CONTROLS] = "Asigna teclas y botones de mando de cada jugador.",
        [MI_SAVE] = "Escribe la configuracion actual en el .ini; sin guardar, los cambios valen solo esta sesion.",
        [MI_RESET] = "Apaga y enciende la maquina.", [MI_QUIT] = "Cierra el juego.",
        [MI_WIDE] = "Mas escenario a los lados en el combate, sin estirar. Apagado el juego es la recreativa.",
        [MI_WIDTH] = "Con la pantalla ancha: 512 es 16:9; 688 ajusta a 3440x1440.",
        [MI_HUD] = "Centro: donde lo compone el juego. Bordes: pegado a los lados de la ventana.",
        [MI_LOGIC] = "Los luchadores usan todo el ancho. CAMBIA EL JUEGO; apagado es el original.",
        [MI_STAGE] = "Camara fija y escenario entero para ultraanchos. CAMBIA EL JUEGO.",
        [MI_HFR] = "Cuadros intermedios al refresco del monitor. Solo presentacion; pide mas de 90 Hz.",
        [MI_DEVTOOL] = "Menus ocultos de Capcom; Enter reinicia. CAMBIA EL JUEGO.",
        [MI_SCALE] = "Tamano de la ventana: x1 a x6, o ajustar (maximizada).",
        [MI_FULLSCREEN] = "Ventana o pantalla completa.", [MI_INTSCALE] = "Solo multiplos enteros: pixeles sin deformar (con aspecto de pixel).",
        [MI_ASPECT] = "4:3 como el monitor de la recreativa, o pixeles cuadrados.", [MI_SMOOTH] = "Filtro lineal en vez de pixeles nitidos.",
        [MI_VSYNC] = "Sincroniza con el monitor. Apagado, el HFR sigue su refresco con reloj propio.",
        [MI_TR_ENABLED] = "Interruptor general. CAMBIA EL JUEGO; apagado = original.",
        [MI_TR_TIME] = "El temporizador del asalto se queda en 99.",
        [MI_TR_REFILL] = "Normales / tras un retardo / siempre llenas (sin KO).",
        [MI_TR_DELAY] = "Cuadros sin dano antes de llenar (60 = 1 s).",
        [MI_TR_LOCK] = "J2 ignora teclado y mando: lo maneja el dummy.",
        [MI_TR_AUTOVS] = "Desde el titulo: monedas y versus; J2 elige solo.",
        [MI_TR_DUMMY] = "Guion: clave script de [training] en el .ini.",
        [MI_TR_INPUTS] = "Palanca, botones e historial de J1 y J2 en pantalla.",
        [MI_TR_HITBOXES] = "Azul dano, rojo ataque, verde empuje, naranja proyectil.",
        [MI_SOUND] = "Apaga o enciende el sonido de esta sesion.", [MI_VOLUME] = "Izquierda y derecha cambian de 10 en 10.",
        [MI_KB1] = "Teclas del jugador 1.", [MI_KB2] = "Teclas del jugador 2.", [MI_PAD1] = "Botones del mando 1 (jugador 1).", [MI_PAD2] = "Botones del mando 2 (jugador 2).",
        [MI_RESTORE_ALL] = "Deja teclas y mandos como vienen por omision.",
        [MI_RESTORE_PAGE] = "Deja esta pagina como viene por omision.", [MI_BACK] = "Vuelve a la pagina anterior (Esc).",
    };
    const int id = menu_id(a, idx), wide = sf2_frame_width(a->sf2) > SF2_SCREEN_W;
    *label = id == MI_BIND ? bind_labels[idx] : labels[id];
    *hint = id == MI_BIND ? (a->bind_pad ? "Enter y pulsa un boton del mando. Esc cancela, Supr borra." : "Enter y pulsa una tecla. Esc cancela, Supr borra.") : hints[id];
    val[0] = 0;
    switch (id) {
    case MI_PRESET: { int c = current_preset(a); snprintf(val, n, "< %s >", c >= 0 ? presets[c].name : "Personalizado"); break; }
    case MI_WIDE: snprintf(val, n, "%s", yesno(wide)); break;
    case MI_WIDTH: snprintf(val, n, "< %d px >", a->cfg.wide_width); break;
    case MI_HUD: snprintf(val, n, "%s", a->cfg.hud_edges ? "bordes" : "centro"); break;
    case MI_LOGIC: snprintf(val, n, "%s", yesno(a->cfg.wide_logic)); break;
    case MI_STAGE: snprintf(val, n, "%s", yesno(a->cfg.stage_full)); break;
    case MI_HFR: snprintf(val, n, "%s", yesno(a->cfg.hfr)); break;
    case MI_TR_ENABLED: snprintf(val, n, "%s", yesno(a->cfg.tr_enabled)); break;
    case MI_TR_TIME: snprintf(val, n, "%s", yesno(a->cfg.tr_infinite_time)); break;
    case MI_TR_REFILL: snprintf(val, n, "< %s >", a->cfg.tr_refill == 0 ? "normales" : a->cfg.tr_refill == 1 ? "tras un retardo" : "siempre llenas"); break;
    case MI_TR_DELAY: snprintf(val, n, "< %d >", a->cfg.tr_refill_delay); break;
    case MI_TR_LOCK: snprintf(val, n, "%s", yesno(a->cfg.tr_lock_p2)); break;
    case MI_TR_AUTOVS: snprintf(val, n, "%s", yesno(a->cfg.tr_auto_vs)); break;
    case MI_TR_DUMMY: {
        static const char *const names[] = { "quieto", "agachado", "salta", "se cubre de pie", "se cubre agachado", "camina hacia J1", "se aleja de J1", "guion" };
        snprintf(val, n, "< %s >", names[a->cfg.tr_dummy & 7]);
        break;
    }
    case MI_TR_INPUTS: snprintf(val, n, "%s", yesno(a->cfg.show_inputs)); break;
    case MI_TR_HITBOXES: snprintf(val, n, "%s", yesno(a->cfg.show_hitboxes)); break;
    case MI_DEVTOOL: snprintf(val, n, "< %s >%s", dev_tool_names[a->dev_sel], a->dev_sel != sf2_dev_tool(a->sf2) ? " Enter" : ""); break;
    case MI_SCALE: if (a->cfg.scale > 0) snprintf(val, n, "< x%d >", a->cfg.scale); else snprintf(val, n, "< ajustar >"); break;
    case MI_FULLSCREEN: snprintf(val, n, "%s", yesno(a->win && (SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN))); break;
    case MI_INTSCALE: snprintf(val, n, "%s", yesno(a->cfg.integer_scale)); break;
    case MI_ASPECT: snprintf(val, n, "%s", a->cfg.aspect43 ? "4:3" : "pixel"); break;
    case MI_SMOOTH: snprintf(val, n, "%s", yesno(a->cfg.smooth)); break;
    case MI_VSYNC: snprintf(val, n, "%s", yesno(a->cfg.vsync)); break;
    case MI_SOUND: snprintf(val, n, "%s", yesno(a->audio && a->cfg.audio)); break;
    case MI_VOLUME: snprintf(val, n, "< %d >", a->cfg.volume); break;
    case MI_BIND: {
        const int act = bind_action(a, idx);
        const char *s = a->bind_pad ? a->cfg.pads[act] : a->cfg.keys[act];
        if (a->capture == act) snprintf(val, n, "pulsa...");
        else snprintf(val, n, "%s", s[0] ? s : "(sin asignar)");
        break;
    }
    default: break;
    }
}

static const char *page_title(const App *a) {
    static char buf[64];
    if (a->menu_page != PG_BINDS) return pages[a->menu_page].title;
    snprintf(buf, sizeof buf, "%s jugador %d", a->bind_pad ? "Mando" : "Teclado", a->bind_player + 1);
    return buf;
}

static void menu_draw(App *a) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(a->ren, &w, &h);
    const int n = menu_count(a), cols = 58, rows = 21;                    /* alto fijo: el de la página más larga (asignaciones), así el tamaño no cambia entre páginas */
    float k = SDL_floorf(fminf((float)w / (cols * 8 + 16), (float)h / (rows * 10 + 16)));
    if (k < 1.0f) k = 1.0f;
    if (k > 4.0f) k = 4.0f;
    SDL_SetRenderScale(a->ren, k, k);
    const float vw = (float)w / k, vh = (float)h / k, pw = cols * 8 + 16, ph = rows * 10 + 12;
    const float x0 = (vw - pw) / 2, y0 = (vh - ph) / 2;
    SDL_SetRenderDrawBlendMode(a->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 150);
    SDL_FRect all = { 0, 0, vw, vh };
    SDL_RenderFillRect(a->ren, &all);
    SDL_SetRenderDrawColor(a->ren, 16, 20, 40, 235);
    SDL_FRect panel = { x0, y0, pw, ph };
    SDL_RenderFillRect(a->ren, &panel);
    SDL_SetRenderDrawColor(a->ren, 200, 170, 60, 255);
    SDL_RenderRect(a->ren, &panel);
    char title[96];
    snprintf(title, sizeof title, a->menu_page == PG_MAIN ? "SF2Recomp - Menu" : "SF2Recomp - Menu > %s", page_title(a));
    SDL_SetRenderDrawColor(a->ren, 255, 214, 80, 255);
    SDL_RenderDebugText(a->ren, x0 + 8, y0 + 8, title);
    const char *hint = "";
    for (int i = 0; i < n; i++) {
        const char *label, *h2;
        char val[64];
        menu_item(a, i, &label, val, sizeof val, &h2);
        const float y = y0 + 8 + (float)(i + 2) * 10;
        if (i == a->menu_sel) {
            SDL_SetRenderDrawColor(a->ren, 70, 90, 170, 255);
            SDL_FRect bar = { x0 + 4, y - 1, pw - 8, 10 };
            SDL_RenderFillRect(a->ren, &bar);
            hint = h2;
        }
        SDL_SetRenderDrawColor(a->ren, 255, 255, 255, 255);
        SDL_RenderDebugText(a->ren, x0 + 12, y, label);
        if (val[0]) {
            if (!strcmp(val, "si") || !strcmp(val, "bordes")) SDL_SetRenderDrawColor(a->ren, 120, 255, 140, 255);
            else if (!strcmp(val, "no") || !strcmp(val, "(sin asignar)")) SDL_SetRenderDrawColor(a->ren, 170, 170, 170, 255);
            else if (!strcmp(val, "pulsa...")) SDL_SetRenderDrawColor(a->ren, 255, 120, 120, 255);
            else SDL_SetRenderDrawColor(a->ren, 255, 214, 80, 255);
            SDL_RenderDebugText(a->ren, x0 + pw - 12 - (float)strlen(val) * 8, y, val);
        }
    }
    SDL_SetRenderDrawColor(a->ren, 190, 200, 230, 255);
    SDL_RenderDebugText(a->ren, x0 + 8, y0 + ph - 20, hint);
    SDL_RenderDebugText(a->ren, x0 + 8, y0 + ph - 10, a->capture >= 0 ? "Pulsa la tecla o el boton   Esc: cancelar   Supr: borrar" : "Flechas: elegir y cambiar   Enter: aceptar   Esc: atras");
    SDL_SetRenderScale(a->ren, 1.0f, 1.0f);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *ev) {
    App *a = appstate;
    if (ev->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    input_handle_event(a->input, ev);
    if (a->menu) {
        /* con el menú abierto, el teclado y el mando manejan el menú y nada más */
        if (ev->type == SDL_EVENT_KEY_DOWN)
            return menu_key(a, ev->key.scancode) ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
        if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            return menu_pad(a, (SDL_GamepadButton)ev->gbutton.button) ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
        if (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
            menu_axis(a, ev->gaxis.axis, ev->gaxis.value);
        return SDL_APP_CONTINUE;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
        a->menu = 1;
        return SDL_APP_CONTINUE;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        SDL_Scancode sc = ev->key.scancode;
        if (input_hotkey_matches(a->input, SF2_HK_MENU, sc)) { a->menu = 1; return SDL_APP_CONTINUE; }
        if (input_hotkey_matches(a->input, SF2_HK_QUIT, sc)) return SDL_APP_SUCCESS;
        if (input_hotkey_matches(a->input, SF2_HK_SAVE_STATE, sc)) save_state(a);
        else if (input_hotkey_matches(a->input, SF2_HK_LOAD_STATE, sc)) load_state(a);
        else if (input_hotkey_matches(a->input, SF2_HK_SLOT_PREV, sc)) { a->slot = (a->slot + 9) % 10; osd(a, "Ranura %d", a->slot); }
        else if (input_hotkey_matches(a->input, SF2_HK_SLOT_NEXT, sc)) { a->slot = (a->slot + 1) % 10; osd(a, "Ranura %d", a->slot); }
        else if (input_hotkey_matches(a->input, SF2_HK_PAUSE, sc)) { a->paused = !a->paused; a->next_ns = 0; osd(a, a->paused ? "Pausa" : "Continua"); }
        else if (input_hotkey_matches(a->input, SF2_HK_FULLSCREEN, sc)) { if (a->win) toggle_fullscreen(a); }
        else if (input_hotkey_matches(a->input, SF2_HK_WIDESCREEN, sc)) toggle_widescreen(a);
        else if (input_hotkey_matches(a->input, SF2_HK_STAGE_FULL, sc)) set_stage_full(a, !a->cfg.stage_full);
        else if (input_hotkey_matches(a->input, SF2_HK_HFR, sc)) set_hfr(a, !a->cfg.hfr);
        else if (input_hotkey_matches(a->input, SF2_HK_WIDE_LOGIC, sc)) set_wide_logic(a, !a->cfg.wide_logic);
        else if (input_hotkey_matches(a->input, SF2_HK_TRAINING, sc)) set_training(a, !a->cfg.tr_enabled);
        else if (input_hotkey_matches(a->input, SF2_HK_SHOW_INPUTS, sc)) set_show_inputs(a, !a->cfg.show_inputs);
        else if (input_hotkey_matches(a->input, SF2_HK_SHOW_HITBOXES, sc)) set_show_hitboxes(a, !a->cfg.show_hitboxes);
        else if (input_hotkey_matches(a->input, SF2_HK_SCREENSHOT, sc)) write_screenshot(a);
        else if (input_hotkey_matches(a->input, SF2_HK_RESET, sc)) { sf2_reset(a->sf2); a->halted = 0; a->new_frame = 1; osd(a, "Reinicio"); }
        else if (sc == SDL_SCANCODE_RETURN && (ev->key.mod & SDL_KMOD_ALT) && a->win) toggle_fullscreen(a);
    }
    return SDL_APP_CONTINUE;
}

/* secuencia de --menu-test: abre el menú, asigna P1 arriba = W y abajo = K, sube la escala a x4 y apaga el VSync */
static const SDL_Scancode menu_test_keys[] = {
    SDL_SCANCODE_M, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_RETURN,   /* Controles */
    SDL_SCANCODE_RETURN, SDL_SCANCODE_RETURN, SDL_SCANCODE_W,                                       /* teclado 1, Arriba, tecla W */
    SDL_SCANCODE_DOWN, SDL_SCANCODE_RETURN, SDL_SCANCODE_K,                                          /* Abajo, tecla K */
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_UP, SDL_SCANCODE_UP, SDL_SCANCODE_RETURN, /* atrás, atrás, Video */
    SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_RETURN, /* escala +1, VSync */
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_ESCAPE,                                                       /* atrás hasta cerrar el menú (un tercer Esc saldría del juego) */
};

static int menu_test_step(App *a) {
    const int n = (int)(sizeof menu_test_keys / sizeof *menu_test_keys);
    if (a->menu_test_step < n) {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = menu_test_keys[a->menu_test_step++];
        SDL_PushEvent(&ev);
        return 0;
    }
    const int ok = !strcmp(a->cfg.keys[SF2_ACT_P1_UP], "W") && !strcmp(a->cfg.keys[SF2_ACT_P1_DOWN], "K") && a->cfg.scale == 4 && !a->cfg.vsync && !a->menu;
    printf("menu-test: arriba=%s abajo=%s escala=%d vsync=%d menu=%d -> %s\n", a->cfg.keys[SF2_ACT_P1_UP], a->cfg.keys[SF2_ACT_P1_DOWN], a->cfg.scale, a->cfg.vsync,
           a->menu, ok ? "OK" : "FALLA");
    return 1;
}

/* ---- SDL_AppIterate ------------------------------------------------------------------------------------------ */
SDL_AppResult SDL_AppIterate(void *appstate) {
    App *a = appstate;
    if (a->bench)
        return bench_step(a);
    uint64_t now = SDL_GetTicksNS();
    a->fast = a->nothrottle || input_hotkey_down(a->input, SF2_HK_FAST_FORWARD);
    int done = 0;
    if (a->halted && a->max_frames)
        return SDL_APP_FAILURE;          /* partida guiada (--frames, pruebas): un fallo termina el proceso con error */
    if (a->paused || a->halted || a->menu) {
        SDL_DelayNS(8000000);
    } else if (a->fast) {
        if (a->audio) SDL_ClearAudioStream(a->audio);
        uint64_t t0 = now;
        do { done = run_one_frame(a, 0); } while (!done && !a->halted && SDL_GetTicksNS() - t0 < FF_BUDGET_NS);
        a->next_ns = 0;
    } else if (a->audio) {
        int n = 0, q = audio_queued_ms(a);
        if (a->iters++ > 30) {
            if (a->iters == 32) a->aq_min_ms = a->aq_max_ms = q;
            if (q < a->aq_min_ms) a->aq_min_ms = q;
            if (q > a->aq_max_ms) a->aq_max_ms = q;
            if (q < 4) a->underruns++;
        }
        /* con HFR cada vuelta (un refresco del monitor) emula como mucho un cuadro, para que se enseñen todos; solo si el audio se vacía (menos de 12 ms) se
         * emulan más */
        while (!done && !a->halted && n < AUDIO_MAX_ITER && audio_queued_ms(a) < AUDIO_TARGET_MS &&
               (n == 0 || !a->cfg.hfr || audio_queued_ms(a) < 12)) {
            done = run_one_frame(a, 1);
            n++;
        }
        a->frames_by_iter[n > 4 ? 4 : n]++;
        if (n == 0) SDL_DelayNS(1000000);
    } else {
        if (a->next_ns == 0 || now > a->next_ns + 8 * a->frame_ns) a->next_ns = now;
        int n = 0;
        while (!done && !a->halted && a->next_ns <= now && n < 3) {
            done = run_one_frame(a, 0);
            a->next_ns += a->frame_ns;
            n++;
        }
        if (n == 0) {
            uint64_t wait = a->next_ns - now;
            SDL_DelayNS(wait < 2000000 ? wait : 2000000);
        }
    }
    if (sf2_flip_requested(a->sf2) && !a->quiet) {
        static int warned;
        if (!warned) { fprintf(stderr, "aviso: el juego pidio el volteo de pantalla, no implementado (D24)\n"); warned = 1; }
    }
    if (a->menu_test && a->frames_run >= 400 && menu_test_step(a)) done = 1;
    if (a->menu_png && !a->menu && a->frames_run >= 400 && !a->menu_shot_done) {
        a->menu = 1;                                       /* páginas 0-5 son las del menú; 6, 7, 8, 9 las asignaciones de teclado 1 y 2 y de mando 1 y 2 */
        if (a->menu_shot_page >= 6) { a->menu_page = PG_BINDS; a->bind_player = (a->menu_shot_page - 6) & 1; a->bind_pad = (a->menu_shot_page - 6) >> 1; }
        else a->menu_page = a->menu_shot_page;
    }
    update_title(a, SDL_GetTicksNS());
    if (a->cfg.hfr && (!a->cfg.vsync || a->soft_pace)) {
        /* sin vsync nadie frena el dibujado: se sigue el refresco del monitor (120 Hz si no se sabe) con un reloj propio */
        const uint64_t period = (uint64_t)(1e9 / (a->refresh_hz > 0 ? a->refresh_hz : 120.0));
        const uint64_t t_now = SDL_GetTicksNS();
        if (a->next_draw_ns == 0 || t_now > a->next_draw_ns + 4 * period) a->next_draw_ns = t_now;
        if (a->next_draw_ns > t_now) SDL_DelayNS(a->next_draw_ns - t_now);
        a->next_draw_ns += period;
    }
    {
        /* sin vsync ni HFR nadie frena el bucle: se dibuja solo si hay un cuadro nuevo, el menú o un aviso en pantalla, o cada 50 ms (por si la ventana lo pide) */
        const uint64_t t = SDL_GetTicksNS();
        const int busy = a->new_frame || a->menu || (a->osd[0] && t < a->osd_until_ns) || t - a->last_draw_ns > 50000000ull;
        if (a->cfg.vsync || a->cfg.hfr || busy) {
            draw(a);
            a->last_draw_ns = t;
        }
    }
    if (a->menu_shot_done) done = 1;
    return done ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    App *a = appstate;
    (void)result;
    if (a->stats && a->audio)
        printf("audio: cola entre %d y %d ms (objetivo %d), %llu huecos; cuadros por vuelta 0:%llu 1:%llu 2:%llu 3:%llu 4:%llu\n",
               a->aq_min_ms, a->aq_max_ms, AUDIO_TARGET_MS, (unsigned long long)a->underruns,
               (unsigned long long)a->frames_by_iter[0], (unsigned long long)a->frames_by_iter[1],
               (unsigned long long)a->frames_by_iter[2], (unsigned long long)a->frames_by_iter[3],
               (unsigned long long)a->frames_by_iter[4]);
    if (a->stats && a->cfg.hfr)
        printf("hfr: monitor de %.0f Hz%s, presentados %.0f por segundo (SDL_RenderPresent medio %.2f ms); %llu cuadros intermedios dibujados; reparto de t en octavos (0 a 7):  %llu %llu %llu %llu %llu %llu %llu %llu\n", a->refresh_hz, a->refresh_hz > 0 && a->refresh_hz < 1.5 * SF2_FRAME_HZ ? " (sin efecto: hace falta mas de 90 Hz)" : "", a->measured_hz, a->present_n ? (double)a->present_ns / (double)a->present_n / 1e6 : 0.0, (unsigned long long)a->hfr_draws,
               (unsigned long long)a->hfr_hist[0], (unsigned long long)a->hfr_hist[1], (unsigned long long)a->hfr_hist[2], (unsigned long long)a->hfr_hist[3],
               (unsigned long long)a->hfr_hist[4], (unsigned long long)a->hfr_hist[5], (unsigned long long)a->hfr_hist[6], (unsigned long long)a->hfr_hist[7]);
    if (a->stats)
        printf("juego: %llu VBlank emulados, %.1f por segundo en la ultima ventana de 1 s; %llu vueltas pasaron mas de un VBlank (maximo %d)\n",
               (unsigned long long)a->frames_run, a->fps, (unsigned long long)a->multi_vblank_runs, a->max_vblanks_run);
    if (a->audio) SDL_DestroyAudioStream(a->audio);
    if (a->audio_dev) SDL_CloseAudioDevice(a->audio_dev);
    input_destroy(a->input);
    if (a->tex) SDL_DestroyTexture(a->tex);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    if (a->win) SDL_DestroyWindow(a->win);
    sf2_destroy(a->sf2);
    free(a->png_frames);
    free(a->rescue);
    SDL_Quit();
}
