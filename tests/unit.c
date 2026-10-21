/*
 * unit.c - pruebas unitarias del nucleo y de la plataforma que no necesitan ROMs: CRC-32, inflate, zip, catalogo
 * de ROMs, estados guardados, configuracion, dipswitches y supuestos de portabilidad.
 *
 *   sf2_unit_tests            (sale con 0 si todo pasa)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "crc32.h"
#include "mem.h"
#include "oki6295.h"
#include "romset.h"
#include "sound.h"
#include "state.h"
#include "z80.h"
#include "zipread.h"
#include "vectors.h"

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* el mismo texto que tests/gen_vectors.py */
static void gen_text(uint8_t *out, size_t n) {
    static const char *vocab[12] = { "street", "fighter", "capcom", "hadoken", "shoryuken", "ryu", "ken", "blanka", "guile", "zangief", " ", "\n" };
    uint32_t x = 12345;
    size_t len = 0;
    while (len < n) {
        x = x * 1103515245u + 12345u;
        const char *w = vocab[(x >> 16) % 12];
        for (; *w && len < n; w++) out[len++] = (uint8_t)*w;
    }
}

static void test_crc(void) {
    CHECK(sf2_crc32(0, "123456789", 9) == 0xcbf43926u);
    CHECK(sf2_crc32(0, "", 0) == 0);
    uint32_t c = sf2_crc32(0, "1234", 4);
    CHECK(sf2_crc32(c, "56789", 5) == 0xcbf43926u);           /* acumulado por partes */
}

static void test_inflate(void) {
    uint8_t raw[VEC_RAW_LEN], out[VEC_RAW_LEN];
    gen_text(raw, sizeof raw);
    CHECK(sf2_crc32(0, raw, sizeof raw) == VEC_RAW_CRC);
    struct { const char *name; const uint8_t *data; size_t len; } v[] = {
        { "dinamico", vec_dynamic, sizeof vec_dynamic }, { "fijo", vec_fixed, sizeof vec_fixed }, { "stored", vec_stored, sizeof vec_stored },
    };
    for (size_t i = 0; i < 3; i++) {
        memset(out, 0, sizeof out);
        CHECK(sf2_inflate(v[i].data, v[i].len, out, sizeof out) == 0);
        CHECK(memcmp(out, raw, sizeof raw) == 0);
        /* flujo cortado y salida demasiado corta: error, sin desbordar */
        CHECK(sf2_inflate(v[i].data, v[i].len / 2, out, sizeof out) != 0);
        CHECK(sf2_inflate(v[i].data, v[i].len, out, sizeof out - 1) != 0);
    }
    /* basura: nunca debe fallar con algo distinto de un codigo de error */
    uint32_t x = 99;
    for (int k = 0; k < 2000; k++) {
        uint8_t junk[64];
        for (size_t i = 0; i < sizeof junk; i++) { x = x * 1664525u + 1013904223u; junk[i] = (uint8_t)(x >> 24); }
        sf2_inflate(junk, sizeof junk, out, sizeof out);
    }
    CHECK(1);
}

/* zip de prueba en memoria: entradas "stored" y "deflate" con cabeceras locales y directorio central */
static size_t put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return 2; }
static size_t put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); return 4; }

typedef struct ZEnt { const char *name; const uint8_t *data; size_t clen; uint32_t usize, crc; int method; uint32_t off; } ZEnt;

static size_t build_zip(uint8_t *buf, ZEnt *e, int n) {
    size_t p = 0;
    for (int i = 0; i < n; i++) {
        e[i].off = (uint32_t)p;
        p += put32(buf + p, 0x04034b50u); p += put16(buf + p, 20); p += put16(buf + p, 0); p += put16(buf + p, (unsigned)e[i].method);
        p += put32(buf + p, 0); p += put32(buf + p, e[i].crc); p += put32(buf + p, (uint32_t)e[i].clen); p += put32(buf + p, e[i].usize);
        p += put16(buf + p, (unsigned)strlen(e[i].name)); p += put16(buf + p, 0);
        memcpy(buf + p, e[i].name, strlen(e[i].name)); p += strlen(e[i].name);
        memcpy(buf + p, e[i].data, e[i].clen); p += e[i].clen;
    }
    size_t cd = p;
    for (int i = 0; i < n; i++) {
        p += put32(buf + p, 0x02014b50u); p += put16(buf + p, 20); p += put16(buf + p, 20); p += put16(buf + p, 0); p += put16(buf + p, (unsigned)e[i].method);
        p += put32(buf + p, 0); p += put32(buf + p, e[i].crc); p += put32(buf + p, (uint32_t)e[i].clen); p += put32(buf + p, e[i].usize);
        p += put16(buf + p, (unsigned)strlen(e[i].name)); p += put16(buf + p, 0); p += put16(buf + p, 0); p += put16(buf + p, 0); p += put16(buf + p, 0);
        p += put32(buf + p, 0); p += put32(buf + p, e[i].off);
        memcpy(buf + p, e[i].name, strlen(e[i].name)); p += strlen(e[i].name);
    }
    size_t cdlen = p - cd;
    p += put32(buf + p, 0x06054b50u); p += put16(buf + p, 0); p += put16(buf + p, 0); p += put16(buf + p, (unsigned)n); p += put16(buf + p, (unsigned)n);
    p += put32(buf + p, (uint32_t)cdlen); p += put32(buf + p, (uint32_t)cd); p += put16(buf + p, 0);
    return p;
}

static void test_zip(void) {
    uint8_t raw[VEC_RAW_LEN];
    gen_text(raw, sizeof raw);
    const uint8_t small[] = "hola";
    ZEnt e[3] = {
        { "dir/a.txt", small, 4, 4, sf2_crc32(0, small, 4), 0, 0 },
        { "texto.bin", vec_dynamic, sizeof vec_dynamic, VEC_RAW_LEN, VEC_RAW_CRC, 8, 0 },
        { "fijo.bin", vec_fixed, sizeof vec_fixed, VEC_RAW_LEN, VEC_RAW_CRC, 8, 0 },
    };
    static uint8_t buf[8192];
    size_t len = build_zip(buf, e, 3);
    Zip z;
    char err[128];
    CHECK(zip_open(&z, buf, len, err, sizeof err) == 0);
    CHECK(z.count == 3);
    uint8_t out[VEC_RAW_LEN];
    CHECK(zip_extract(&z, 0, out, err, sizeof err) == 0 && memcmp(out, "hola", 4) == 0);
    CHECK(zip_extract(&z, 1, out, err, sizeof err) == 0 && memcmp(out, raw, sizeof raw) == 0);
    CHECK(zip_extract(&z, 2, out, err, sizeof err) == 0 && memcmp(out, raw, sizeof raw) == 0);
    zip_close(&z);
    /* CRC del directorio central distinto del real: se detecta al extraer */
    e[0].crc ^= 1;
    len = build_zip(buf, e, 3);
    CHECK(zip_open(&z, buf, len, err, sizeof err) == 0);
    CHECK(zip_extract(&z, 0, out, err, sizeof err) != 0);
    CHECK(zip_extract(&z, 1, out, err, sizeof err) == 0);
    zip_close(&z);
    e[0].crc ^= 1;
    /* no es un zip / truncado */
    CHECK(zip_open(&z, (const uint8_t *)"no soy un zip, de verdad que no", 31, err, sizeof err) != 0);
    CHECK(zip_open(&z, buf, 10, err, sizeof err) != 0);
    len = build_zip(buf, e, 3);
    CHECK(zip_open(&z, buf, len - 5, err, sizeof err) != 0);     /* sin el final del directorio central */
    /* datos comprimidos corrompidos */
    len = build_zip(buf, e, 3);
    buf[e[1].off + 30 + strlen(e[1].name) + 10] ^= 0xff;
    if (zip_open(&z, buf, len, err, sizeof err) == 0) {
        CHECK(zip_extract(&z, 1, out, err, sizeof err) != 0);
        zip_close(&z);
    }
}

/* origen de ROMs sintetico: archivos con el tamano de una ROM pero contenido que no corresponde */
typedef struct FakeSrc { const char *names[3]; uint32_t sizes[3]; uint8_t *data; } FakeSrc;
static int fake_get(void *ctx, size_t i, RomFile *o) {
    FakeSrc *s = ctx;
    if (i >= 3) return 0;
    o->name = s->names[i]; o->size = s->sizes[i]; o->crc = 0; o->crc_known = 0;
    return 1;
}
static int fake_read(void *ctx, size_t i, uint8_t *dst) {
    FakeSrc *s = ctx;
    memset(dst, 0x5a, s->sizes[i]);
    return 0;
}

static void test_romset(void) {
    /* catalogo: 23 entradas con CRC distintos, tamanos validos y destinos que no se pisan */
    uint64_t total = 0;
    for (int i = 0; i < ROMSET_COUNT; i++) {
        const RomInfo *a = &romset_catalogue[i];
        total += a->size;
        for (int j = i + 1; j < ROMSET_COUNT; j++) {
            CHECK(a->crc != romset_catalogue[j].crc);
            CHECK(strcmp(a->name, romset_catalogue[j].name) != 0);
        }
        if (a->kind == ROMKIND_PROGRAM) CHECK(a->dest + 2u * a->size <= 0x100000 + a->lane);
        if (a->kind == ROMKIND_GFX) CHECK(a->dest + 8u * (a->size / 2) <= 0x600000 + 6);
    }
    CHECK(total == 8u * 0x20000 + 12u * 0x80000 + 0x10000 + 2u * 0x20000);
    CHECK(romset_candidate_count() == 2u * ROMSET_COUNT);
    /* contenido equivocado en archivos con nombre de ROM: faltan las 23, y el informe nombra el CRC malo */
    FakeSrc fs = { { "sf2e.30g", "sf2_09.bin", "otra_cosa.bin" }, { 0x20000, 0x10000, 0x20000 }, NULL };
    RomSource src = { &fs, fake_get, fake_read };
    RomSet rs;
    char report[4096];
    romset_init(&rs);
    int missing = romset_load(&rs, &src, report, sizeof report);
    CHECK(missing == ROMSET_COUNT);
    CHECK(strstr(report, "Faltan 23 de las 23") != NULL);
    CHECK(strstr(report, "sf2e_30g.11e") != NULL && strstr(report, "fe39ee33") != NULL);
    CHECK(strstr(report, "el archivo \"sf2e.30g\" tiene el CRC") != NULL);
    romset_free(&rs);
}

static void test_state_stream(void) {
    uint8_t buf[64];
    StateStream s;
    uint8_t a = 0xab; uint16_t b = 0xbeef; uint32_t c = 0xdeadbeefu; uint64_t d = 0x0123456789abcdefull; float f = -1.5f; int32_t g = -7;
    state_begin_save(&s, buf, sizeof buf);
    st_u8(&s, &a); st_u16(&s, &b); st_u32(&s, &c); st_u64(&s, &d); st_f32(&s, &f); st_i32(&s, &g);
    CHECK(!s.error && s.pos == 1 + 2 + 4 + 8 + 4 + 4);
    CHECK(buf[1] == 0xef && buf[2] == 0xbe && buf[3] == 0xef && buf[6] == 0xde);       /* little-endian explicito */
    size_t used = s.pos;
    a = 0; b = 0; c = 0; d = 0; f = 0; g = 0;
    state_begin_load(&s, buf, used);
    st_u8(&s, &a); st_u16(&s, &b); st_u32(&s, &c); st_u64(&s, &d); st_f32(&s, &f); st_i32(&s, &g);
    CHECK(!s.error && a == 0xab && b == 0xbeef && c == 0xdeadbeefu && d == 0x0123456789abcdefull && f == -1.5f && g == -7);
    state_begin_load(&s, buf, used - 1);
    st_u8(&s, &a); st_u16(&s, &b); st_u32(&s, &c); st_u64(&s, &d); st_f32(&s, &f); st_i32(&s, &g);
    CHECK(s.error);                                         /* final prematuro */
    state_begin_save(&s, buf, 4);
    st_u64(&s, &d);
    CHECK(s.error);                                         /* no cabe */
    state_begin_save(&s, NULL, 0);
    st_u64(&s, &d); st_u32(&s, &c);
    CHECK(!s.error && s.pos == 12);                         /* modo de medida */
}

static void test_config(void) {
    Sf2Config c, d;
    config_defaults(&c);
    uint8_t a, b, dc;
    config_dsw(&c, &a, &b, &dc);
    CHECK(a == MEM_DSWA_DEFAULT && b == MEM_DSWB_DEFAULT && dc == MEM_DSWC_DEFAULT);   /* los dipswitches de MAME */
    /* escribir y volver a leer da lo mismo */
    char text[16384], warn[512];
    size_t n = config_format(&c, text, sizeof text);
    CHECK(n > 0 && n < sizeof text);
    config_defaults(&d);
    d.scale = 7; d.volume = 3; d.difficulty = 6; d.free_play = 1; strcpy(d.keys[SF2_ACT_P1_UP], "W");
    CHECK(config_parse(&d, text, warn, sizeof warn) == 0);
    CHECK(d.scale == 3 && d.volume == 80 && d.difficulty == 3 && d.free_play == 0 && strcmp(d.keys[SF2_ACT_P1_UP], "Up") == 0);
    /* cambios con nombre */
    const char *ini = "[dip]\ndifficulty = 7\nfree_play = yes\ncoin_a = 2c1c\ntwo_coins_to_start = on\ndemo_sounds = off\nallow_continue = no\n"
                      "[video]\nscale = 5\naspect = 4:3\n[audio]\nvolume = 25\nrate = 44100\n[keys]\np1_up = W, Up\n[pad]\np1_hp = lefttrigger\n"
                      "[hotkeys]\nreset = F9\n[paths]\nrom = /tmp/x.zip\n";
    config_defaults(&d);
    CHECK(config_parse(&d, ini, warn, sizeof warn) == 0);
    config_dsw(&d, &a, &b, &dc);
    CHECK((b & 7) == 0);                                    /* dificultad 7 = la mas dificil = 000 */
    CHECK((dc & 0x04) == 0);                                /* free play */
    CHECK((a & 7) == 0x02 && (a & 0x40) == 0);              /* 2 monedas por credito, 2 monedas para empezar */
    CHECK((dc & 0x20) != 0 && (dc & 0x40) != 0);            /* sin sonidos en demo y sin continuar */
    CHECK(d.scale == 5 && d.aspect43 && d.volume == 25 && d.audio_rate == 44100);
    CHECK(strcmp(d.keys[SF2_ACT_P1_UP], "W, Up") == 0 && strcmp(d.pads[SF2_ACT_P1_HP], "lefttrigger") == 0);
    CHECK(strcmp(d.hotkeys[SF2_HK_RESET], "F9") == 0 && strcmp(d.rom_path, "/tmp/x.zip") == 0);
    /* valores invalidos: avisos y se conserva el valor anterior */
    config_defaults(&d);
    int w = config_parse(&d, "[audio]\nvolume = 300\n[video]\nscale = abc\nfoo = 1\n[dip]\ncoin_a = 9c9c\ndifficulty = -1\n[keys]\np3_up = X\nsinigual\n[zzz]\nx=1\n", warn, sizeof warn);
    CHECK(w == 8);
    CHECK(d.volume == 80 && d.scale == 3 && d.coin_a == 3 && d.difficulty == 3);
    CHECK(strstr(warn, "volume") != NULL);
}

static void test_portability(void) {
    /* orden de bytes: el par de registros del Z80 debe corresponder a SF2_LITTLE_ENDIAN */
    Z80Pair p;
    p.w = 0x1234;
    CHECK(p.b.h == 0x12 && p.b.l == 0x34);
    CHECK(sizeof(Z80Pair) == 2);
    /* la tabla de pasos del OKI, escrita a mano, coincide con floor(16 * 1,1^n) de MAME (que usa pow) */
    for (int n = 0; n <= 48; n++)
        CHECK(oki6295_step_table[n] == (int)floor(16.0 * pow(11.0 / 10.0, (double)n)));
}

int main(void) {
    test_crc();
    test_inflate();
    test_zip();
    test_romset();
    test_state_stream();
    test_config();
    test_portability();
    printf("sf2_unit_tests: %d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
