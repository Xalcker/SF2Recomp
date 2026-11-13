/* hostio.c - acceso a archivos del anfitrión con stdio (ver hostio.h). */
#include "hostio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"
#include "zipread.h"

/* Windows: las rutas del juego llegan en UTF-8 (SDL entrega así los argumentos y la carpeta de preferencias) y fopen, remove y
 * rename las leen en la página de códigos ANSI: con una ñ o un acento en la ruta (el nombre de usuario, sin ir más lejos) no se
 * podía guardar la configuración ni los estados.  Se pasan a UTF-16 y se usan las funciones anchas.  Si la ruta no es UTF-8
 * válido (herramientas de consola, que reciben los argumentos en ANSI) se deja a stdio como estaba. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <wchar.h>
#define HOST_PATH_MAX 1300
static int wide_path(const char *s, wchar_t *out, int cap) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, cap) > 0;
}
#endif

FILE *hostio_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    wchar_t wp[HOST_PATH_MAX], wm[8];
    if (wide_path(path, wp, HOST_PATH_MAX) && wide_path(mode, wm, 8))
        return _wfopen(wp, wm);
#endif
    return fopen(path, mode);
}

static void remove_file(const char *path) {
#ifdef _WIN32
    wchar_t wp[HOST_PATH_MAX];
    if (wide_path(path, wp, HOST_PATH_MAX)) {
        _wremove(wp);
        return;
    }
#endif
    remove(path);
}

/* `tmp` pasa a ser `path`, sustituyendo al que hubiera; 0 o -1 */
static int replace_file(const char *tmp, const char *path) {
#ifdef _WIN32
    /* rename no sobrescribe en Windows; MoveFileEx sí, y sin el instante en que el archivo anterior ya se borró y el nuevo aún no
     * tiene su nombre */
    wchar_t wt[HOST_PATH_MAX], wp[HOST_PATH_MAX];
    if (wide_path(tmp, wt, HOST_PATH_MAX) && wide_path(path, wp, HOST_PATH_MAX)) {
        if (MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING))
            return 0;
        errno = EACCES;
        return -1;
    }
    remove(path);
#endif
    return rename(tmp, path);
}

int hostio_read_file(const char *path, uint8_t **data, size_t *len) {
    FILE *f = hostio_fopen(path, "rb");
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
    FILE *f = hostio_fopen(tmp, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove_file(tmp); return -1; }
    if (replace_file(tmp, path) != 0) { remove_file(tmp); return -1; }
    return 0;
}

int hostio_is_zip(const char *path) {
    FILE *f = hostio_fopen(path, "rb");
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
    const GameDef *game;
    size_t count;
    char path[2 * ROMSET_MAX][1100];
    uint32_t size[2 * ROMSET_MAX];
    size_t cand[2 * ROMSET_MAX];           /* índice de candidato de cada archivo presente */
} DirSource;

static int dir_get(void *ctx, size_t i, RomFile *out) {
    DirSource *d = ctx;
    if (i >= d->count)
        return 0;
    out->name = romset_candidate_name(d->game, d->cand[i]);
    out->size = d->size[i];
    out->crc = 0;
    out->crc_known = 0;
    return 1;
}

static int dir_read(void *ctx, size_t i, uint8_t *dst) {
    DirSource *d = ctx;
    FILE *f = hostio_fopen(d->path[i], "rb");
    if (!f)
        return -1;
    size_t n = fread(dst, 1, d->size[i], f);
    int extra = fgetc(f);                   /* el archivo no debe ser más largo de lo anunciado */
    fclose(f);
    return n == d->size[i] && extra == EOF ? 0 : -1;
}

static void dir_scan(DirSource *d, const GameDef *game, const char *dir) {
    d->dir = dir;
    d->game = game;
    d->count = 0;
    for (size_t c = 0; c < romset_candidate_count(game); c++) {
        if ((c & 1) && strcmp(romset_candidate_name(game, c), romset_candidate_name(game, c - 1)) == 0)
            continue;                       /* sin nombre antiguo: el mismo archivo no se cuenta dos veces */
        char *p = d->path[d->count];
        snprintf(p, sizeof d->path[0], "%s/%s", dir, romset_candidate_name(game, c));
        FILE *f = hostio_fopen(p, "rb");
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
    dir_scan(d, rs->game, path);
    if (d->count == 0) {
        snprintf(report, report_size, "No se encontro ninguna ROM de %s en \"%s\" (se busca un zip o una carpeta con los "
                 "archivos con los nombres de MAME, por ejemplo %s)\n", rs->game->id, path, rs->game->roms[0].name);
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
