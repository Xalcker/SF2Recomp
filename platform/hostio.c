/* hostio.c - acceso a archivos del anfitrión con stdio (ver hostio.h). */
#include "hostio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"
#include "zipread.h"

int hostio_read_file(const char *path, uint8_t **data, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t cap = 1 << 16, n = 0;
    uint8_t *buf = malloc(cap + 1);
    if (!buf) { fclose(f); return -1; }
    for (;;) {
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (n < cap)
            break;                      /* fin de archivo (o error, que se distingue abajo) */
        uint8_t *nb = realloc(buf, cap * 2 + 1);
        if (!nb) { free(buf); fclose(f); return -1; }
        buf = nb;
        cap *= 2;
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) { free(buf); return -1; }
    buf[n] = 0;
    *data = buf;
    *len = n;
    return 0;
}

int hostio_write_file(const char *path, const void *data, size_t len) {
    char tmp[1100];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
        return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return -1; }
#ifdef _WIN32
    remove(path);                       /* rename no sobrescribe en Windows */
#endif
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

int hostio_is_zip(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned char b[4] = { 0 };
    size_t n = fread(b, 1, 4, f);
    fclose(f);
    return n == 4 && b[0] == 'P' && b[1] == 'K';
}

/* ---- carpeta con los archivos sueltos ---------------------------------------------------------------- */
typedef struct DirSource {
    const char *dir;
    size_t count;
    char path[2 * ROMSET_COUNT][1100];
    uint32_t size[2 * ROMSET_COUNT];
    size_t cand[2 * ROMSET_COUNT];         /* índice de candidato de cada archivo presente */
} DirSource;

static int dir_get(void *ctx, size_t i, RomFile *out) {
    DirSource *d = ctx;
    if (i >= d->count)
        return 0;
    out->name = romset_candidate_name(d->cand[i]);
    out->size = d->size[i];
    out->crc = 0;
    out->crc_known = 0;
    return 1;
}

static int dir_read(void *ctx, size_t i, uint8_t *dst) {
    DirSource *d = ctx;
    FILE *f = fopen(d->path[i], "rb");
    if (!f)
        return -1;
    size_t n = fread(dst, 1, d->size[i], f);
    int extra = fgetc(f);                   /* el archivo no debe ser más largo de lo anunciado */
    fclose(f);
    return n == d->size[i] && extra == EOF ? 0 : -1;
}

static void dir_scan(DirSource *d, const char *dir) {
    d->dir = dir;
    d->count = 0;
    for (size_t c = 0; c < romset_candidate_count(); c++) {
        char *p = d->path[d->count];
        snprintf(p, sizeof d->path[0], "%s/%s", dir, romset_candidate_name(c));
        FILE *f = fopen(p, "rb");
        if (!f)
            continue;
        long size = -1;
        if (fseek(f, 0, SEEK_END) == 0)
            size = ftell(f);
        fclose(f);
        if (size <= 0)
            continue;
        d->size[d->count] = (uint32_t)size;
        d->cand[d->count] = c;
        d->count++;
    }
}

int hostio_load_romset(const char *path, RomSet *rs, char *report, size_t report_size) {
    if (hostio_is_zip(path)) {
        uint8_t *data;
        size_t len;
        if (hostio_read_file(path, &data, &len) != 0) {
            snprintf(report, report_size, "No se pudo leer %s\n", path);
            return -1;
        }
        int rc = romset_load_zip_memory(rs, data, len, report, report_size);
        free(data);
        return rc;
    }
    DirSource *d = malloc(sizeof *d);
    if (!d)
        return -1;
    dir_scan(d, path);
    if (d->count == 0) {
        snprintf(report, report_size, "No se encontro ninguna ROM de SF2 en \"%s\" (se busca un zip o una carpeta con los "
                 "archivos con los nombres de MAME, por ejemplo sf2e_30g.11e o sf2e.30g)\n", path);
        free(d);
        return -1;
    }
    RomSource src = { d, dir_get, dir_read };
    int rc = romset_load(rs, &src, report, report_size);
    free(d);
    return rc;
}

/* ---- entradas por cuadro ------------------------------------------------------------------------------ */
int cps1_load_inputs(Cps1 *m, const char *path) {
    uint8_t *data;
    size_t len;
    if (hostio_read_file(path, &data, &len) != 0)
        return -1;
    char err[512];
    int rc = cps1_parse_inputs(m, (const char *)data, path, err, sizeof err);
    if (rc == -2)
        fputs(err, stderr);
    free(data);
    return rc;
}

/* ---- PNG ---------------------------------------------------------------------------------------------- */
int hostio_write_png(const char *path, const uint32_t *frame, int w, int h) {
    uint8_t *png;
    size_t len;
    if (png_encode_rgb(frame, w, h, &png, &len) != 0)
        return -1;
    int rc = hostio_write_file(path, png, len);
    free(png);
    return rc;
}

int hostio_write_video_png(const Video *v, const char *path) {
    return hostio_write_png(path, v->frame, v->w, VIDEO_H);
}
