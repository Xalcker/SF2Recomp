/* romset.c - catálogo de ROMs de SF2 y carga con verificación por CRC (ver romset.h). */
#include "romset.h"
#include "crc32.h"
#include "zipread.h"
#include "sf2_port.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ROM_START( sf2 ) de MAME 0.289 (src/mame/capcom/cps1.cpp) */
const RomInfo romset_catalogue[ROMSET_COUNT] = {
    /* programa del 68000: pares (par, impar) cada 256 KiB */
    { "sf2e_30g.11e", "sf2e.30g",    0xfe39ee33u, 0x20000, ROMKIND_PROGRAM, 0x00000, 0 },
    { "sf2e_37g.11f", "sf2e.37g",    0xfb92cd74u, 0x20000, ROMKIND_PROGRAM, 0x00000, 1 },
    { "sf2e_31g.12e", "sf2e.31g",    0x69a0a301u, 0x20000, ROMKIND_PROGRAM, 0x40000, 0 },
    { "sf2e_38g.12f", "sf2e.38g",    0x5e22db70u, 0x20000, ROMKIND_PROGRAM, 0x40000, 1 },
    { "sf2e_28g.9e",  "sf2e.28g",    0x8bf9f1e5u, 0x20000, ROMKIND_PROGRAM, 0x80000, 0 },
    { "sf2e_35g.9f",  "sf2e.35g",    0x626ef934u, 0x20000, ROMKIND_PROGRAM, 0x80000, 1 },
    { "sf2_29b.10e",  "sf2_29a.bin", 0xbb4af315u, 0x20000, ROMKIND_PROGRAM, 0xc0000, 0 },
    { "sf2_36b.10f",  "sf2_36a.bin", 0xc02a13ebu, 0x20000, ROMKIND_PROGRAM, 0xc0000, 1 },
    /* gráficos: ROM_LOAD64_WORD, una palabra de cada ROM cada 8 bytes de la región de 6 MiB */
    { "sf2-5m.4a",    "sf2_06.bin",  0x22c9cc8eu, 0x80000, ROMKIND_GFX, 0x000000, 0 },
    { "sf2-7m.6a",    "sf2_08.bin",  0x57213be8u, 0x80000, ROMKIND_GFX, 0x000002, 0 },
    { "sf2-1m.3a",    "sf2_05.bin",  0xba529b4fu, 0x80000, ROMKIND_GFX, 0x000004, 0 },
    { "sf2-3m.5a",    "sf2_07.bin",  0x4b1b33a8u, 0x80000, ROMKIND_GFX, 0x000006, 0 },
    { "sf2-6m.4c",    "sf2_15.bin",  0x2c7e2229u, 0x80000, ROMKIND_GFX, 0x200000, 0 },
    { "sf2-8m.6c",    "sf2_17.bin",  0xb5548f17u, 0x80000, ROMKIND_GFX, 0x200002, 0 },
    { "sf2-2m.3c",    "sf2_14.bin",  0x14b84312u, 0x80000, ROMKIND_GFX, 0x200004, 0 },
    { "sf2-4m.5c",    "sf2_16.bin",  0x5e9cd89au, 0x80000, ROMKIND_GFX, 0x200006, 0 },
    { "sf2-13m.4d",   "sf2_25.bin",  0x994bfa58u, 0x80000, ROMKIND_GFX, 0x400000, 0 },
    { "sf2-15m.6d",   "sf2_27.bin",  0x3e66ad9du, 0x80000, ROMKIND_GFX, 0x400002, 0 },
    { "sf2-9m.3d",    "sf2_24.bin",  0xc1befaa8u, 0x80000, ROMKIND_GFX, 0x400004, 0 },
    { "sf2-11m.5d",   "sf2_26.bin",  0x0627c831u, 0x80000, ROMKIND_GFX, 0x400006, 0 },
    /* sonido */
    { "sf2_9.12a",    "sf2_09.bin",  0xa4823a1bu, 0x10000, ROMKIND_Z80, 0x00000, 0 },
    { "sf2_18.11c",   "sf2_18.bin",  0x7f162009u, 0x20000, ROMKIND_OKI, 0x00000, 0 },
    { "sf2_19.12c",   "sf2_19.bin",  0xbeade53fu, 0x20000, ROMKIND_OKI, 0x20000, 0 },
};

void romset_init(RomSet *rs) {
    memset(rs, 0, sizeof *rs);
}

void romset_free(RomSet *rs) {
    for (int i = 0; i < ROMSET_COUNT; i++) {
        free(rs->data[i]);
        rs->data[i] = NULL;
    }
}

size_t romset_candidate_count(void) { return 2 * ROMSET_COUNT; }

const char *romset_candidate_name(size_t i) {
    const RomInfo *r = &romset_catalogue[(i / 2) % ROMSET_COUNT];
    return (i & 1) ? r->alt_name : r->name;
}

static const char *base_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    return b;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

static int size_is_rom(uint32_t size) {
    for (int i = 0; i < ROMSET_COUNT; i++)
        if (romset_catalogue[i].size == size)
            return 1;
    return 0;
}

/* entrada libre del catálogo con ese CRC y tamaño, o -1 */
static int find_entry(const RomSet *rs, uint32_t crc, uint32_t size) {
    for (int i = 0; i < ROMSET_COUNT; i++)
        if (!rs->data[i] && romset_catalogue[i].crc == crc && romset_catalogue[i].size == size)
            return i;
    return -1;
}

/* entrada del catálogo cuyo nombre (actual o antiguo) es `name` */
static int find_by_name(const char *name) {
    for (int i = 0; i < ROMSET_COUNT; i++)
        if (ieq(name, romset_catalogue[i].name) || ieq(name, romset_catalogue[i].alt_name))
            return i;
    return -1;
}

typedef struct Report { char *buf; size_t cap, len; } Report;

static void rep(Report *r, const char *fmt, ...) SF2_PRINTF(2, 3);
static void rep(Report *r, const char *fmt, ...) {
    if (!r->buf || r->len + 1 >= r->cap)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(r->buf + r->len, r->cap - r->len, fmt, ap);
    va_end(ap);
    if (n > 0)
        r->len = r->len + (size_t)n < r->cap ? r->len + (size_t)n : r->cap - 1;
}

int romset_load(RomSet *rs, const RomSource *src, char *report, size_t report_size) {
    Report r = { report, report_size, 0 };
    if (report && report_size)
        report[0] = 0;
    char wrong[ROMSET_COUNT][96] = { { 0 } };        /* diagnóstico por entrada: archivo con su nombre pero otro CRC */
    uint32_t wrong_crc[ROMSET_COUNT] = { 0 };
    char unreadable[512] = "";
    RomFile f;
    uint8_t *tmp = NULL;
    size_t tmp_cap = 0;

    for (size_t i = 0; src->get(src->ctx, i, &f); i++) {
        if (!size_is_rom(f.size))
            continue;
        int by_name = find_by_name(base_name(f.name));
        int idx = -1;
        if (f.crc_known) {
            idx = find_entry(rs, f.crc, f.size);
            if (idx < 0) {
                /* con el nombre de una ROM pero otro CRC: se anota sin leerlo */
                if (by_name >= 0 && !rs->data[by_name]) {
                    snprintf(wrong[by_name], sizeof wrong[by_name], "%s", base_name(f.name));
                    wrong_crc[by_name] = f.crc;
                }
                continue;
            }
        }
        if (f.size > tmp_cap) {
            uint8_t *n = realloc(tmp, f.size);
            if (!n) { snprintf(unreadable, sizeof unreadable, "sin memoria para leer %s", f.name); break; }
            tmp = n;
            tmp_cap = f.size;
        }
        if (src->read(src->ctx, i, tmp) != 0) {
            snprintf(unreadable + strlen(unreadable), sizeof unreadable - strlen(unreadable), "%s%s", unreadable[0] ? ", " : "", base_name(f.name));
            continue;
        }
        uint32_t crc = sf2_crc32(0, tmp, f.size);
        if (f.crc_known && crc != f.crc) {
            snprintf(unreadable + strlen(unreadable), sizeof unreadable - strlen(unreadable), "%s%s (CRC distinto del declarado en el zip)", unreadable[0] ? ", " : "", base_name(f.name));
            continue;
        }
        idx = find_entry(rs, crc, f.size);
        if (idx < 0) {
            if (by_name >= 0 && !rs->data[by_name]) {
                snprintf(wrong[by_name], sizeof wrong[by_name], "%s", base_name(f.name));
                wrong_crc[by_name] = crc;
            }
            continue;
        }
        rs->data[idx] = malloc(f.size);
        if (!rs->data[idx]) { snprintf(unreadable, sizeof unreadable, "sin memoria"); break; }
        memcpy(rs->data[idx], tmp, f.size);
    }
    free(tmp);

    int missing = 0;
    for (int i = 0; i < ROMSET_COUNT; i++)
        if (!rs->data[i])
            missing++;
    if (missing) {
        rep(&r, "Faltan %d de las %d ROMs de Street Fighter II (World 910522, set \"sf2\" de MAME):\n", missing, ROMSET_COUNT);
        for (int i = 0; i < ROMSET_COUNT; i++) {
            if (rs->data[i])
                continue;
            const RomInfo *ri = &romset_catalogue[i];
            rep(&r, "  - %s (o %s), %u KiB, CRC %08x", ri->name, ri->alt_name, ri->size / 1024, ri->crc);
            if (wrong[i][0])
                rep(&r, " -> el archivo \"%s\" tiene el CRC %08x: no es esta revision o esta danado", wrong[i], wrong_crc[i]);
            rep(&r, "\n");
        }
    }
    if (unreadable[0])
        rep(&r, "No se pudieron leer: %s\n", unreadable);
    return missing;
}

void romset_build_program(const RomSet *rs, uint8_t *image) {
    for (int i = 0; i < ROMSET_COUNT; i++) {
        const RomInfo *ri = &romset_catalogue[i];
        if (ri->kind != ROMKIND_PROGRAM || !rs->data[i])
            continue;
        for (uint32_t k = 0; k < ri->size; k++)
            image[ri->dest + 2u * k + (uint32_t)ri->lane] = rs->data[i][k];
    }
}

/* ---- origen sobre un zip en memoria ---------------------------------------------------------------- */
static int zip_get(void *ctx, size_t i, RomFile *out) {
    const Zip *z = ctx;
    if (i >= z->count)
        return 0;
    out->name = z->entries[i].name;
    out->size = z->entries[i].usize;
    out->crc = z->entries[i].crc;
    out->crc_known = 1;
    return 1;
}

static int zip_read(void *ctx, size_t i, uint8_t *dst) {
    return zip_extract(ctx, i, dst, NULL, 0);
}

void romset_source_from_zip(RomSource *src, const struct Zip *zip) {
    src->ctx = (void *)zip;
    src->get = zip_get;
    src->read = zip_read;
}

int romset_load_zip_memory(RomSet *rs, const uint8_t *data, size_t size, char *report, size_t report_size) {
    Zip zip;
    char err[96];
    if (zip_open(&zip, data, size, err, sizeof err) != 0) {
        if (report && report_size)
            snprintf(report, report_size, "No se pudo abrir el zip: %s\n", err);
        return -1;
    }
    RomSource src;
    romset_source_from_zip(&src, &zip);
    int missing = romset_load(rs, &src, report, report_size);
    zip_close(&zip);
    return missing;
}
