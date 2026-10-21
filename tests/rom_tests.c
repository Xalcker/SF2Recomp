/*
 * rom_tests.c - pruebas que necesitan las ROMs del usuario: carga desde zip y desde carpeta, verificacion contra el
 * codigo recompilado, informe de ROMs que faltan y estados guardados (determinismo, rechazo de estados danados).
 *
 *   sf2_rom_tests <sf2.zip|carpeta> <secuencia_de_entradas.txt> [<otra carpeta o zip con las mismas ROMs>]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cps1.h"
#include "crc32.h"
#include "hostio.h"
#include "machine.h"
#include "romset.h"
#include "sf2.h"
#include "zipread.h"
#include "recomp_funcs.h"

#define TEST_WIDE_W 512         /* ancho de las pruebas de pantalla ancha (16:9) */
static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int load_all(const char *path, RomSet *rs) {
    char report[4096];
    romset_init(rs);
    int missing = hostio_load_romset(path, rs, report, sizeof report);
    if (missing) fprintf(stderr, "%s: %s", path, report);
    return missing;
}

/* origen que oculta una ROM del zip/carpeta real (para probar el informe de ROMs que faltan) */
typedef struct Hide { RomSource inner; uint32_t crc; const char *tmp_name; } Hide;

static void test_roms(const char *path, const char *path2) {
    RomSet rs;
    CHECK(load_all(path, &rs) == 0);
    for (int i = 0; i < ROMSET_COUNT; i++) CHECK(rs.data[i] != NULL);
    static uint8_t prog[ROM_SIZE];
    romset_build_program(&rs, prog);
    CHECK(sf2_crc32(0, prog, ROM_SIZE) == recomp_rom_crc32);          /* las ROMs son las del codigo recompilado */
    if (path2) {
        RomSet rs2;
        CHECK(load_all(path2, &rs2) == 0);
        for (int i = 0; i < ROMSET_COUNT; i++)
            CHECK(rs.data[i] && rs2.data[i] && memcmp(rs.data[i], rs2.data[i], romset_catalogue[i].size) == 0);
        romset_free(&rs2);
    }
    romset_free(&rs);
}

static int g_hide_crc;
static int hide_get(void *ctx, size_t i, RomFile *o) {
    Hide *h = ctx;
    int r = h->inner.get(h->inner.ctx, i, o);
    if (r && o->crc_known && o->crc == (uint32_t)g_hide_crc) o->crc ^= 0xffff;      /* el zip "declara" otro CRC */
    return r;
}
static int hide_read(void *ctx, size_t i, uint8_t *dst) { Hide *h = ctx; return h->inner.read(h->inner.ctx, i, dst); }

static void test_missing(const char *path) {
    if (!hostio_is_zip(path)) return;
    uint8_t *data;
    size_t len;
    CHECK(hostio_read_file(path, &data, &len) == 0);
    Zip z;
    char err[128], report[4096];
    CHECK(zip_open(&z, data, len, err, sizeof err) == 0);
    Hide h;
    romset_source_from_zip(&h.inner, &z);
    g_hide_crc = (int)romset_catalogue[ROMSET_COUNT - 1].crc;          /* sf2_19.12c */
    RomSource src = { &h, hide_get, hide_read };
    RomSet rs;
    romset_init(&rs);
    int missing = romset_load(&rs, &src, report, sizeof report);
    CHECK(missing == 1);
    CHECK(strstr(report, "Faltan 1 de las 23") != NULL && strstr(report, "sf2_19.12c") != NULL);
    /* con una ROM menos el nucleo no arranca y lo dice */
    Sf2 *s = NULL;
    char e2[512];
    CHECK(sf2_create(&s, &rs, NULL, e2, sizeof e2) == -1 && s == NULL);
    romset_free(&rs);
    zip_close(&z);
    free(data);
}

typedef struct Trace { uint32_t video[400], audio[400], wram[400]; uint64_t frame[400]; int n; } Trace;

static void run_trace(Sf2 *s, int frames, Trace *t) {
    t->n = 0;
    for (int i = 0; i < frames; i++) {
        CHECK(sf2_run_frame(s) > 0);
        size_t na;
        const float *a = sf2_audio(s, &na);
        t->video[i] = sf2_crc32(0, sf2_frame(s), SF2_SCREEN_W * SF2_SCREEN_H * 4);
        t->audio[i] = sf2_crc32(0, a, na * sizeof(float));
        t->frame[i] = sf2_frame_count(s);
        sf2_audio_clear(s);
        t->n++;
    }
}

static int same_trace(const Trace *a, const Trace *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->video[i] != b->video[i] || a->audio[i] != b->audio[i] || a->frame[i] != b->frame[i]) {
            fprintf(stderr, "traza distinta en la vuelta %d: cuadro %llu/%llu video %08x/%08x audio %08x/%08x\n", i,
                    (unsigned long long)a->frame[i], (unsigned long long)b->frame[i], a->video[i], b->video[i], a->audio[i], b->audio[i]);
            return 0;
        }
    return 1;
}

static Sf2 *make_machine(const char *rom, const char *script_text) {
    RomSet rs;
    if (load_all(rom, &rs)) return NULL;
    Sf2 *s;
    char err[512];
    if (sf2_create(&s, &rs, NULL, err, sizeof err) != 0) { fprintf(stderr, "sf2_create: %s\n", err); romset_free(&rs); return NULL; }
    romset_free(&rs);
    if (sf2_load_input_script(s, script_text, "inputs", err, sizeof err) < 0) { fprintf(stderr, "%s\n", err); return NULL; }
    return s;
}

static void put32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void refresh_crc(uint8_t *st, size_t len) { put32le(st + 16, sf2_crc32(0, st + 24, len - 24)); }

/* guarda el estado actual y comprueba que sigue igual despues de un intento de carga rechazado */
static void expect_rejected(Sf2 *s, const uint8_t *bad, size_t badlen, const char *what, const char *fragment) {
    size_t n = sf2_state_size(s);
    uint8_t *before = malloc(n), *after = malloc(n);
    CHECK(sf2_state_save(s, before, n) == (int)n);
    char err[256] = "";
    int rc = sf2_state_load(s, bad, badlen, err, sizeof err);
    CHECK(rc == -1);
    if (rc != -1) fprintf(stderr, "  (no se rechazo: %s)\n", what);
    if (fragment) CHECK(strstr(err, fragment) != NULL);
    CHECK(sf2_state_save(s, after, n) == (int)n);
    CHECK(memcmp(before, after, n) == 0);              /* la maquina quedo como estaba */
    free(before);
    free(after);
}

static void test_states(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script);
    Sf2 *b = make_machine(rom, script);
    CHECK(a && b);
    if (!a || !b) return;
    Trace ta, tb, tc;
    for (int i = 0; i < 1800; i++) CHECK(sf2_run_frame(a) > 0);       /* hasta el combate */
    sf2_audio_clear(a);
    size_t n = sf2_state_size(a);
    uint8_t *st = malloc(n);
    CHECK(sf2_state_save(a, st, n) == (int)n);
    run_trace(a, 360, &ta);                                            /* seis segundos mas, de la maquina original */
    /* 1. otra maquina, recien creada, retoma exactamente igual */
    char err[256];
    CHECK(sf2_state_load(b, st, n, err, sizeof err) == 0);
    run_trace(b, 360, &tb);
    CHECK(same_trace(&ta, &tb));
    /* 2. la original rebobina y repite lo mismo */
    CHECK(sf2_state_load(a, st, n, err, sizeof err) == 0);
    run_trace(a, 360, &tc);
    CHECK(same_trace(&ta, &tc));
    /* 3. un estado guardado por la maquina B tras cargar y correr es igual de valido para A (guardar y cargar encadenados) */
    size_t n2 = sf2_state_size(b);
    uint8_t *st2 = malloc(n2);
    CHECK(sf2_state_save(b, st2, n2) == (int)n2);
    CHECK(sf2_state_load(a, st2, n2, err, sizeof err) == 0);
    Trace td, te;
    run_trace(a, 120, &td);
    run_trace(b, 120, &te);
    CHECK(same_trace(&td, &te));
    /* 4. estados rechazados: la maquina no cambia */
    uint8_t *bad = malloc(n);
    memcpy(bad, st, n);
    expect_rejected(a, bad, n - 1, "truncado", "truncado");
    bad[100] ^= 0x40;
    expect_rejected(a, bad, n, "byte cambiado (CRC)", "danado");
    memcpy(bad, st, n);
    bad[0] = 'X';
    expect_rejected(a, bad, n, "sin marca", "no es un estado");
    memcpy(bad, st, n);
    bad[4] = 9;
    expect_rejected(a, bad, n, "otra version", "version");
    memcpy(bad, st, n);
    put32le(bad + 8, recomp_rom_crc32 ^ 1);
    expect_rejected(a, bad, n, "otro codigo", "otro codigo recompilado");
    /* contenido invalido con CRC correcto: bandera de la CPU fuera de rango y PC que no es una direccion de la tabla */
    memcpy(bad, st, n);
    bad[24 + 72] = 5;
    refresh_crc(bad, n);
    expect_rejected(a, bad, n, "bandera 5", "invalido");
    memcpy(bad, st, n);
    put32le(bad + 24 + 64, 1);
    refresh_crc(bad, n);
    expect_rejected(a, bad, n, "pc sin etiqueta", "invalido");
    free(bad);
    free(st);
    free(st2);
    sf2_destroy(a);
    sf2_destroy(b);
}

/* guardar en cualquier punto de la partida debe funcionar (arranque, atract, seleccion, combate, continuar...) */
static void test_state_sweep(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script);
    CHECK(a != NULL);
    if (!a) return;
    size_t cap = sf2_state_size(a) + 4096;
    uint8_t *buf = malloc(cap);
    int failed = 0;
    for (int f = 0; f < 4000; f++) {
        CHECK(sf2_run_frame(a) > 0);
        sf2_audio_clear(a);
        if (f % 37 == 0 || f < 40) {
            int n = sf2_state_save(a, buf, cap);
            if (n <= 0 && failed++ < 5) fprintf(stderr, "no se pudo guardar el estado en el cuadro %d\n", f);
            CHECK(n > 0);
        }
    }
    free(buf);
    sf2_destroy(a);
}

static void test_reset_and_dips(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script);
    CHECK(a != NULL);
    if (!a) return;
    Trace t1, t2;
    run_trace(a, 120, &t1);
    sf2_reset(a);
    run_trace(a, 120, &t2);
    CHECK(same_trace(&t1, &t2) == 0 || t1.frame[0] == t2.frame[0]);     /* el reinicio vuelve a empezar */
    CHECK(t1.video[119] == t2.video[119]);
    sf2_destroy(a);
}

/* hito 6: una dirección sin código recompilado detiene la máquina con un mensaje claro (nunca en silencio), deja el
 * salto en sf2_dispatch_failure y no estropea los estados: cargar uno anterior la recupera */
static void test_dispatch_failure(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script);
    CHECK(a != NULL);
    if (!a) return;
    for (int i = 0; i < 300; i++) CHECK(sf2_run_frame(a) > 0);
    size_t n = sf2_state_size(a);
    uint8_t *st = malloc(n);
    CHECK(sf2_state_save(a, st, n) == (int)n);
    uint32_t from = 1, to = 1;
    CHECK(sf2_dispatch_failure(a, &from, &to) == 0);
    sf2_debug_jump(a, 0x000102);                        /* tabla de vectores: nunca es código */
    CHECK(sf2_run_frame(a) == -1);
    CHECK(sf2_dispatch_failure(a, &from, &to) == 1);
    CHECK(to == 0x000102);
    CHECK(strstr(sf2_error(a), "salto a 000102") != NULL);
    CHECK(strstr(sf2_error(a), "entry 000102") != NULL);
    CHECK(sf2_run_frame(a) == -1);                      /* sigue detenida */
    char err[256];
    CHECK(sf2_state_load(a, st, n, err, sizeof err) == 0);
    CHECK(sf2_dispatch_failure(a, &from, &to) == 0);
    CHECK(sf2_error(a)[0] == 0);
    CHECK(sf2_run_frame(a) > 0);                        /* y vuelve a correr */
    free(st);
    sf2_destroy(a);
}

/* HFR, fase 0 (docs/HFR.md): cada VBlank guarda el cuadro anterior y el actual; la instantánea anterior es la actual del cuadro previo, y volver a
 * dibujar desde la actual (t = 1) da el mismo cuadro píxel a píxel, con la pantalla estrecha y con la ancha. */
static void test_hfr_snapshot(const char *rom, const char *script) {
    Sf2 *m[2] = { make_machine(rom, script), make_machine(rom, script) }, *ctl = make_machine(rom, script);
    CHECK(m[0] && m[1] && ctl);
    if (!m[0] || !m[1] || !ctl) return;
    sf2_set_video_width(m[1], TEST_WIDE_W);
    sf2_set_hud_edges(m[1], 1);
    static uint16_t last_obj[2][SF2_OBJ_WORDS];
    uint16_t last_scroll[2][6] = { { 0 } };
    int bad_chain = 0, bad_redraw = 0, moved = 0, bad_t1 = 0, bad_state = 0, bad_mid = 0, mid_differs = 0;
    const int frames = 3000;
    for (int i = 0; i < frames; i++) {
        CHECK(sf2_run_frame(ctl) > 0);
        sf2_audio_clear(ctl);
        for (int k = 0; k < 2; k++) {
            CHECK(sf2_run_frame(m[k]) > 0);
            sf2_audio_clear(m[k]);
            static uint16_t po[SF2_OBJ_WORDS], co[SF2_OBJ_WORDS];
            uint16_t ps[6], cs[6];
            sf2_video_snapshot(m[k], 0, po, ps);
            sf2_video_snapshot(m[k], 1, co, cs);
            if (i > 0 && (memcmp(po, last_obj[k], sizeof po) != 0 || memcmp(ps, last_scroll[k], sizeof ps) != 0)) bad_chain++;
            if (memcmp(po, co, sizeof po) != 0 || memcmp(ps, cs, sizeof ps) != 0) moved++;
            memcpy(last_obj[k], co, sizeof co);
            memcpy(last_scroll[k], cs, sizeof cs);
            const size_t n = (size_t)sf2_frame_width(m[k]) * SF2_SCREEN_H * 4;
            const uint32_t before = sf2_crc32(0, sf2_frame(m[k]), n);
            sf2_render_snapshot(m[k]);
            if (sf2_crc32(0, sf2_frame(m[k]), n) != before) bad_redraw++;
            /* HFR fase 1: t = 256 es el cuadro normal; los cuadros intermedios son estables y no cambian la partida ni el cuadro siguiente */
            sf2_render_interp(m[k], 256);
            if (sf2_crc32(0, sf2_frame(m[k]), n) != before) bad_t1++;
            sf2_render_interp(m[k], 128);
            const uint32_t mid = sf2_crc32(0, sf2_frame(m[k]), n);
            sf2_render_interp(m[k], 128);
            if (sf2_crc32(0, sf2_frame(m[k]), n) != mid) bad_mid++;
            if (mid != before) mid_differs++;
            sf2_render_interp(m[k], 256);
        }
        if (i % 500 == 499 && sf2_state_crc(m[0]) != sf2_state_crc(ctl)) bad_state++;      /* dibujar cuadros intermedios no toca la partida */
    }
    CHECK(bad_state == 0);
    CHECK(bad_chain == 0);
    CHECK(bad_redraw == 0);
    CHECK(bad_t1 == 0 && bad_mid == 0);
    CHECK(mid_differs > 500);           /* a t = 0,5 hay cuadros de verdad distintos */
    CHECK(moved > 500);                 /* y los cuadros sí cambian de uno a otro */
    fprintf(stderr, "HFR fases 0-1: %d cadenas rotas, %d redibujados distintos, %d cuadros con instantanea nueva, %d cuadros a t=0,5 distintos del normal\n", bad_chain, bad_redraw, moved, mid_differs);
    sf2_destroy(m[0]);
    sf2_destroy(m[1]);
    sf2_destroy(ctl);
}

/* Pantalla ancha (D53): el ancho del cuadro no cambia la partida y, en cualquier escena, las 384 columnas centrales del
 * cuadro ancho son idénticas píxel a píxel al cuadro de 384.  Las escenas que no se ensanchan llevan barras negras, y el
 * combate sí se ensancha (hay cuadros con algo distinto de negro en las bandas). */
static void test_widescreen(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script), *b = make_machine(rom, script), *c = make_machine(rom, script);
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    CHECK(sf2_frame_width(b) == SF2_SCREEN_W);
    CHECK(sf2_set_video_width(b, 383) != 0 && sf2_set_video_width(b, 513) != 0 && sf2_set_video_width(b, 385) != 0);
    CHECK(sf2_frame_width(b) == SF2_SCREEN_W);
    CHECK(sf2_set_video_width(b, TEST_WIDE_W) == 0 && sf2_frame_width(b) == TEST_WIDE_W);
    sf2_set_video_width(c, TEST_WIDE_W);
    sf2_set_hud_edges(c, 1);                      /* D55: solo puede mover el marcador, que ocupa las 48 primeras filas */
    const int bar = (TEST_WIDE_W - SF2_SCREEN_W) / 2, wb = TEST_WIDE_W;
    int wide_frames = 0, narrow_frames = 0, bad_centre = 0, bad_state = 0, bad_hud = 0, hud_moved = 0, hud_edge_ink = 0;
    const char *env = getenv("SF2_WIDE_FRAMES");        /* corridas largas a mano (por omision 3000 cuadros) */
    const int frames = env && atoi(env) > 0 ? atoi(env) : 3000;
    for (int i = 0; i < frames; i++) {
        CHECK(sf2_run_frame(a) > 0 && sf2_run_frame(b) > 0 && sf2_run_frame(c) > 0);
        sf2_audio_clear(a);
        sf2_audio_clear(b);
        sf2_audio_clear(c);
        const uint32_t *fa = sf2_frame(a), *fb = sf2_frame(b), *fc = sf2_frame(c);
        if (memcmp(fb + 48 * wb, fc + 48 * wb, (size_t)(SF2_SCREEN_H - 48) * wb * sizeof *fb) != 0) bad_hud++;
        if (memcmp(fb, fc, 48 * wb * sizeof *fb) != 0) hud_moved++;
        for (int y = 0; y < 48; y++) for (int x = 0; x < 8; x++) hud_edge_ink |= fc[y * wb + x] && !fb[y * wb + x];
        int side_ink = 0;
        for (int y = 0; y < SF2_SCREEN_H; y++) {
            if (memcmp(fa + y * SF2_SCREEN_W, fb + y * wb + bar, SF2_SCREEN_W * sizeof *fa) != 0) bad_centre++;
            for (int x = 0; x < bar; x++) side_ink |= fb[y * wb + x] | fb[y * wb + bar + SF2_SCREEN_W + x];
        }
        if (side_ink) wide_frames++; else narrow_frames++;
        if (i % 500 == 499 && sf2_state_crc(a) != sf2_state_crc(b)) bad_state++;     /* el ancho no es parte de la partida */
    }
    CHECK(bad_centre == 0);
    CHECK(bad_state == 0);
    CHECK(bad_hud == 0);                    /* el HUD en los bordes solo toca las 48 primeras filas */
    CHECK(hud_moved > 100 && hud_edge_ink);  /* y las toca: el marcador llega al borde izquierdo */
    CHECK(wide_frames > 100);               /* el combate de la secuencia se ve ancho */
    CHECK(narrow_frames > 100);             /* y las demas pantallas, centradas */
    fprintf(stderr, "pantalla ancha: %d cuadros anchos, %d centrados, %d lineas centrales distintas, HUD en bordes: %d cuadros distintos arriba, %d abajo\n", wide_frames, narrow_frames, bad_centre, hud_moved, bad_hud);
    sf2_destroy(a);
    sf2_destroy(b);
    sf2_destroy(c);
}

/* Lógica ancha (D58): apagada, la partida es la original con cualquier ancho; encendida solo actúa con el cuadro ancho; y cambia la partida. */
static void test_wide_logic(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script), *b = make_machine(rom, script), *c = make_machine(rom, script);
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    sf2_set_wide_logic(a, 1);                         /* con el cuadro de 384 no hay píxeles extra */
    CHECK(sf2_wide_logic_extra(a) == 0);
    sf2_set_video_width(b, TEST_WIDE_W);         /* ancho sin lógica: la partida no cambia */
    sf2_set_video_width(c, TEST_WIDE_W);
    sf2_set_wide_logic(c, 1);
    int saw64 = 0, b_nonzero = 0;                       /* los píxeles extra solo valen en las escenas que se ensanchan */
    for (int i = 0; i < 3000; i++) {
        CHECK(sf2_run_frame(a) > 0 && sf2_run_frame(b) > 0 && sf2_run_frame(c) > 0);
        saw64 |= sf2_wide_logic_extra(c) == 64;
        b_nonzero |= sf2_wide_logic_extra(b) != 0;
        sf2_audio_clear(a); sf2_audio_clear(b); sf2_audio_clear(c);
    }
    CHECK(sf2_state_crc(a) == sf2_state_crc(b));
    CHECK(saw64 && !b_nonzero);
    sf2_set_wide_logic(c, 0);                         /* apagada en caliente, sigue corriendo sin problemas */
    CHECK(sf2_wide_logic_extra(c) == 0);
    for (int i = 0; i < 300; i++) { CHECK(sf2_run_frame(c) > 0); sf2_audio_clear(c); }
    sf2_destroy(a);
    sf2_destroy(b);
    sf2_destroy(c);
}

/* Stage completo (D59): encendido cambia la partida y se ve entera; apagado en caliente, el juego vuelve a ser el original. */
static void test_stage_full(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script), *d = make_machine(rom, script), *e = make_machine(rom, script);
    CHECK(a && d && e);
    if (!a || !d || !e) return;
    sf2_set_video_width(d, 768);
    sf2_set_stage_full(d, 1);
    sf2_set_stage_full(d, 0);                         /* encendido y apagado antes de correr: no deja rastro */
    sf2_set_video_width(e, 768);
    sf2_set_stage_full(e, 1);
    int saw_fix = 0;
    for (int i = 0; i < 3000; i++) {
        CHECK(sf2_run_frame(a) > 0 && sf2_run_frame(d) > 0 && sf2_run_frame(e) > 0);
        sf2_audio_clear(a); sf2_audio_clear(d); sf2_audio_clear(e);
        saw_fix |= sf2_wide_logic_extra(e) == 192;
    }
    CHECK(sf2_state_crc(a) == sf2_state_crc(d));
    CHECK(saw_fix);
    CHECK(sf2_state_crc(a) != sf2_state_crc(e));      /* la partida cambia */
    sf2_set_stage_full(e, 0);
    for (int i = 0; i < 300; i++) { CHECK(sf2_run_frame(e) > 0); sf2_audio_clear(e); }
    sf2_destroy(a); sf2_destroy(d); sf2_destroy(e);
}


/* mod de entrenamiento (docs/ENTRENAMIENTO.md): apagado (o solo con el registro de entradas) la partida es identica a la original;
 * encendido, el temporizador no baja, las barras se llenan, J2 ignora el teclado y sigue el guion, y el versus empieza solo */
#define TR_FRAMES 1900
static uint32_t train_run(Sf2 *s, int frames, uint16_t in1) {
    for (int i = 0; i < frames; i++) {
        sf2_set_input(s, 0, in1, 0);
        CHECK(sf2_run_frame(s) > 0);
        sf2_audio_clear(s);
    }
    return sf2_state_crc(s);
}

static void test_training(const char *rom, const char *script) {
    Sf2 *a = make_machine(rom, script), *b = make_machine(rom, script), *c = make_machine(rom, script);
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    char err[200];
    Sf2Training t;
    memset(&t, 0, sizeof t);
    /* 1. con el mod apagado y con solo el registro de entradas, la partida no cambia (el interruptor apagado = juego original) */
    uint32_t base = train_run(a, TR_FRAMES, 0);
    t.track_inputs = 1;
    CHECK(sf2_training_set(b, &t, err, sizeof err) == 0);
    CHECK(train_run(b, TR_FRAMES, 0) == base);
    Sf2InputEvent ev[SF2_TRAIN_HISTORY];
    CHECK(sf2_training_history(b, 0, ev, SF2_TRAIN_HISTORY) > 0);
    /* 2. guiones: los validos se aceptan y los malos se rechazan sin cambiar nada */
    t.enabled = 1; t.dummy = SF2_DUMMY_SCRIPT;
    snprintf(t.script, sizeof t.script, "d:30 df:3 f+lp:2 n:30");
    CHECK(sf2_training_set(c, &t, err, sizeof err) == 0);
    snprintf(t.script, sizeof t.script, "d:30 zz:3");
    CHECK(sf2_training_set(c, &t, err, sizeof err) == -1 && strstr(err, "zz") != NULL);
    snprintf(t.script, sizeof t.script, "d:0");
    CHECK(sf2_training_set(c, &t, err, sizeof err) == -1);
    snprintf(t.script, sizeof t.script, "   ");
    CHECK(sf2_training_set(c, &t, err, sizeof err) == -1);       /* modo script con guion vacio */
    Sf2Training got;
    sf2_training_get(c, &got);
    CHECK(strcmp(got.script, "d:30 df:3 f+lp:2 n:30") == 0 && got.enabled);
    /* 3. versus automatico + sin limite de tiempo + barras llenas + J2 bloqueado y siguiendo el guion */
    memset(&t, 0, sizeof t);
    t.enabled = 1; t.infinite_time = 1; t.refill = 2; t.refill_delay = 60; t.lock_p2 = 1; t.auto_vs = 1; t.track_inputs = 1;
    t.dummy = SF2_DUMMY_SCRIPT;
    snprintf(t.script, sizeof t.script, "d:30 df:3 f+lp:2 n:30");
    Sf2 *d = make_machine(rom, "");
    CHECK(d != NULL);
    if (!d) return;
    CHECK(sf2_training_set(d, &t, err, sizeof err) == 0);
    int fight_frames = 0, p2_dummy_ok = 0, host_leak = 0;
    Mem *mem = &((Machine *)d)->mem;                         /* Sf2 empieza por su Machine (runtime/sf2.c) */
    for (int i = 0; i < 4500; i++) {
        sf2_set_input(d, 0, i % 40 < 3 ? (SF2_IN1_LP | (SF2_IN1_HP << 8) | (SF2_IN1_LEFT << 8)) : 0, 0);   /* J1 pulsa LP, "J2 humano" HP e izquierda */
        CHECK(sf2_run_frame(d) > 0);
        sf2_audio_clear(d);
        const int fight = mem->wram[0xFF8001 - 0xFF0000] == 2 && mem->wram[0xFF8005 - 0xFF0000] == 4 && mem->wram[0xFF8009 - 0xFF0000] == 0x0A;
        if (fight) {
            fight_frames++;
            CHECK(mem->wram[0xFF8ACE - 0xFF0000] == 0x99);
            const int l1 = (int16_t)((mem->wram[0xFF83F0 - 0xFF0000] << 8) | mem->wram[0xFF83F1 - 0xFF0000]);
            const int l2 = (int16_t)((mem->wram[0xFF86F0 - 0xFF0000] << 8) | mem->wram[0xFF86F1 - 0xFF0000]);
            CHECK(l1 == 0x90 && l2 == 0x90);
        }
    }
    CHECK(fight_frames > 1000);                               /* el versus empezo solo y llego al combate */
    CHECK(mem->wram[0xFF86C6 - 0xFF0000] != 0 && mem->wram[0xFF83C6 - 0xFF0000] != 0);   /* J1 y J2 existen: no hay rival de la maquina */
    /* el historial de J2 solo tiene lo del guion/seleccion: nunca HP ni LEFT del "J2 humano" del anfitrion */
    int n = sf2_training_history(d, 1, ev, SF2_TRAIN_HISTORY);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        if (ev[i].mask & SF2_PAD_HP) host_leak = 1;
        if (ev[i].mask == (SF2_PAD_RIGHT | SF2_PAD_LP) || ev[i].mask == (SF2_PAD_LEFT | SF2_PAD_LP) || ev[i].mask == SF2_PAD_DOWN) p2_dummy_ok = 1;
    }
    CHECK(!host_leak);
    CHECK(p2_dummy_ok);
    sf2_destroy(a); sf2_destroy(b); sf2_destroy(c); sf2_destroy(d);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s <sf2.zip|carpeta> <entradas.txt> [otro origen]\n", argv[0]); return 2; }
    uint8_t *script;
    size_t slen;
    if (hostio_read_file(argv[2], &script, &slen) != 0) { fprintf(stderr, "no se puede leer %s\n", argv[2]); return 2; }
    test_roms(argv[1], argc > 3 ? argv[3] : NULL);
    test_missing(argv[1]);
    test_states(argv[1], (const char *)script);
    test_state_sweep(argv[1], (const char *)script);
    test_reset_and_dips(argv[1], (const char *)script);
    test_dispatch_failure(argv[1], (const char *)script);
    test_hfr_snapshot(argv[1], (const char *)script);
    test_widescreen(argv[1], (const char *)script);
    test_wide_logic(argv[1], (const char *)script);
    test_stage_full(argv[1], (const char *)script);
    test_training(argv[1], (const char *)script);
    free(script);
    printf("sf2_rom_tests: %d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
