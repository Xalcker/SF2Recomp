/* romset.c - carga de las ROMs de una variante de SF2 con verificación por CRC (ver romset.h; catálogos en game.c). */
#include "romset.h"
#include "crc32.h"
#include "zipread.h"
#include "sf2_port.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void romset_init(RomSet *rs, const GameDef *game) {
    memset(rs, 0, sizeof *rs);
    rs->game = game;
}

void romset_free(RomSet *rs) {
    for (int i = 0; i < ROMSET_MAX; i++) {
        free(rs->data[i]);
        rs->data[i] = NULL;
    }
}

size_t romset_candidate_count(const GameDef *game) { return 2u * (size_t)game->nroms; }

const char *romset_candidate_name(const GameDef *game, size_t i) {
    const RomInfo *r = &game->roms[(i / 2) % (size_t)game->nroms];
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

static int size_is_rom(const GameDef *g, uint32_t size) {
    for (int i = 0; i < g->nroms; i++)
        if (g->roms[i].size == size)
            return 1;
    return 0;
}

/* entrada libre del catálogo con ese CRC y tamaño, o -1 */
static int find_entry(const RomSet *rs, uint32_t crc, uint32_t size) {
    const GameDef *g = rs->game;
    for (int i = 0; i < g->nroms; i++)
        if (!rs->data[i] && g->roms[i].crc == crc && g->roms[i].size == size)
            return i;
    return -1;
}

/* entrada del catálogo cuyo nombre (actual o antiguo) es `name` */
static int find_by_name(const GameDef *g, const char *name) {
    for (int i = 0; i < g->nroms; i++)
        if (ieq(name, g->roms[i].name) || ieq(name, g->roms[i].alt_name))
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
    const GameDef *g = rs->game;
    char wrong[ROMSET_MAX][96] = { { 0 } };          /* diagnóstico por entrada: archivo con su nombre pero otro CRC */
    uint32_t wrong_crc[ROMSET_MAX] = { 0 };
    char unreadable[512] = "";
    RomFile f;
    uint8_t *tmp = NULL;
    size_t tmp_cap = 0;

    for (size_t i = 0; src->get(src->ctx, i, &f); i++) {
        if (!size_is_rom(g, f.size))
            continue;
        int by_name = find_by_name(g, base_name(f.name));
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
    for (int i = 0; i < g->nroms; i++)
        if (!rs->data[i])
            missing++;
    if (missing) {
        rep(&r, "Faltan %d de las %d ROMs de %s (set \"%s\" de MAME):\n", missing, g->nroms, g->title, g->id);
        for (int i = 0; i < g->nroms; i++) {
            if (rs->data[i])
                continue;
            const RomInfo *ri = &g->roms[i];
            if (strcmp(ri->name, ri->alt_name) != 0)
                rep(&r, "  - %s (o %s), %u KiB, CRC %08x", ri->name, ri->alt_name, ri->size / 1024, ri->crc);
            else
                rep(&r, "  - %s, %u KiB, CRC %08x", ri->name, ri->size / 1024, ri->crc);
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
    const GameDef *g = rs->game;
    for (int i = 0; i < g->nroms; i++) {
        const RomInfo *ri = &g->roms[i];
        if (ri->kind != ROMKIND_PROGRAM || !rs->data[i])
            continue;
        if (ri->lane == ROMLANE_WORD)          /* ROM_LOAD16_WORD_SWAP: cada palabra del archivo tiene los bytes al revés */
            for (uint32_t k = 0; k + 1 < ri->size; k += 2) {
                image[ri->dest + k] = rs->data[i][k + 1];
                image[ri->dest + k + 1] = rs->data[i][k];
            }
        else
            for (uint32_t k = 0; k < ri->size; k++)
                image[ri->dest + 2u * k + (uint32_t)ri->lane] = rs->data[i][k];
    }
}

int romset_complete(const RomSet *rs) {
    for (int i = 0; i < rs->game->nroms; i++)
        if (!rs->data[i])
            return 0;
    return 1;
}

int romset_index(const GameDef *game, RomKind kind, int nth) {
    for (int i = 0; i < game->nroms; i++)
        if (game->roms[i].kind == kind && nth-- == 0)
            return i;
    return -1;
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
