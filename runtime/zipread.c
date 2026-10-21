/* zipread.c - lector de zip en memoria con inflate propio (ver zipread.h). */
#include "zipread.h"
#include "crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- inflate (RFC 1951) --------------------------------------------------------------------------
 * Descodificación por códigos canónicos con tablas de cuentas por longitud, un bit cada vez: lenta frente a
 * una tabla de búsqueda, pero los 7,6 MiB de sf2.zip se abren en una fracción de segundo y el código es
 * corto y fácil de revisar. */
#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct Huff {
    uint16_t count[MAXBITS + 1];
    uint16_t symbol[FIXLCODES];
} Huff;

typedef struct Infl {
    const uint8_t *in;
    size_t inlen, incnt;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outlen, outcnt;
} Infl;

static int get_bits(Infl *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen)
            return -1;
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int decode_sym(Infl *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        int b = get_bits(s, 1);
        if (b < 0)
            return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* construye la tabla; devuelve 0 si el código es completo, <0 si está sobresuscrito, >0 si incompleto */
static int construct(Huff *h, const uint16_t *length, int n) {
    uint16_t offs[MAXBITS + 1];
    memset(h->count, 0, sizeof h->count);
    for (int sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0)
            h->symbol[offs[length[sym]]++] = (uint16_t)sym;
    return left;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint16_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint16_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int inflate_codes(Infl *s, const Huff *lencode, const Huff *distcode) {
    for (;;) {
        int sym = decode_sym(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->outcnt >= s->outlen)
                return -1;
            s->out[s->outcnt++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            int e = get_bits(s, len_extra[sym]);
            if (e < 0)
                return -1;
            size_t len = (size_t)len_base[sym] + (size_t)e;
            int ds = decode_sym(s, distcode);
            if (ds < 0 || ds >= 30)
                return -1;
            e = get_bits(s, dist_extra[ds]);
            if (e < 0)
                return -1;
            size_t dist = (size_t)dist_base[ds] + (size_t)e;
            if (dist > s->outcnt || len > s->outlen - s->outcnt)
                return -1;
            for (size_t k = 0; k < len; k++, s->outcnt++)
                s->out[s->outcnt] = s->out[s->outcnt - dist];
        }
    }
}

static int inflate_stored(Infl *s) {
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen)
        return -1;
    unsigned len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    unsigned nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if (len != (~nlen & 0xffffu))
        return -1;
    if (s->incnt + len > s->inlen || len > s->outlen - s->outcnt)
        return -1;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len;
    s->outcnt += len;
    return 0;
}

static int inflate_fixed(Infl *s) {
    Huff lencode, distcode;
    uint16_t lengths[FIXLCODES];
    int sym = 0;
    for (; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
    construct(&lencode, lengths, FIXLCODES);
    for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
    construct(&distcode, lengths, MAXDCODES);
    return inflate_codes(s, &lencode, &distcode);
}

static int inflate_dynamic(Infl *s) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint16_t lengths[MAXLCODES + MAXDCODES];
    Huff lencode, distcode;
    int nlen = get_bits(s, 5), ndist = get_bits(s, 5), ncode = get_bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0)
        return -1;
    nlen += 257; ndist += 1; ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES)
        return -1;
    int index;
    for (index = 0; index < ncode; index++) {
        int v = get_bits(s, 3);
        if (v < 0)
            return -1;
        lengths[order[index]] = (uint16_t)v;
    }
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0)
        return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode_sym(s, &lencode);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[index++] = (uint16_t)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0)
                    return -1;
                len = lengths[index - 1];
                rep = get_bits(s, 2);
                if (rep < 0) return -1;
                rep += 3;
            } else if (sym == 17) {
                rep = get_bits(s, 3);
                if (rep < 0) return -1;
                rep += 3;
            } else {
                rep = get_bits(s, 7);
                if (rep < 0) return -1;
                rep += 11;
            }
            if (index + rep > nlen + ndist)
                return -1;
            while (rep--)
                lengths[index++] = (uint16_t)len;
        }
    }
    if (lengths[256] == 0)
        return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen - lencode.count[0] != 1))
        return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist - distcode.count[0] != 1))
        return -1;
    return inflate_codes(s, &lencode, &distcode);
}

int sf2_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen) {
    Infl s = { in, inlen, 0, 0, 0, out, outlen, 0 };
    int last;
    do {
        last = get_bits(&s, 1);
        int type = get_bits(&s, 2);
        if (last < 0 || type < 0)
            return -1;
        int rc = type == 0 ? inflate_stored(&s) : type == 1 ? inflate_fixed(&s) : type == 2 ? inflate_dynamic(&s) : -1;
        if (rc != 0)
            return -1;
    } while (!last);
    return s.outcnt == outlen ? 0 : -1;
}

/* ---- zip ---------------------------------------------------------------------------------------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n)
        snprintf(err, n, "%s", msg);
}

int zip_open(Zip *z, const uint8_t *data, size_t size, char *err, size_t errn) {
    memset(z, 0, sizeof *z);
    if (size < 22) { set_err(err, errn, "el archivo es demasiado pequeño para ser un zip"); return -1; }
    /* fin del directorio central: firma 0x06054b50 a menos de 64 KiB + 22 bytes del final */
    size_t lo = size > 22 + 65535 ? size - 22 - 65535 : 0, eocd = (size_t)-1;
    for (size_t p = size - 22 + 1; p-- > lo;)
        if (rd32(data + p) == 0x06054b50u) { eocd = p; break; }
    if (eocd == (size_t)-1) { set_err(err, errn, "no es un archivo zip (sin directorio central)"); return -1; }
    uint16_t count = rd16(data + eocd + 10);
    uint32_t cd_size = rd32(data + eocd + 12), cd_off = rd32(data + eocd + 16);
    if (count == 0xffff || cd_size == 0xffffffffu || cd_off == 0xffffffffu) { set_err(err, errn, "zip64 no soportado"); return -1; }
    if ((uint64_t)cd_off + cd_size > size) { set_err(err, errn, "directorio central fuera del archivo"); return -1; }
    z->entries = calloc(count ? count : 1, sizeof *z->entries);
    if (!z->entries) { set_err(err, errn, "sin memoria"); return -1; }
    z->data = data;
    z->size = size;
    size_t p = cd_off, end = (size_t)cd_off + cd_size;
    for (uint16_t i = 0; i < count; i++) {
        if (p + 46 > end || rd32(data + p) != 0x02014b50u) { set_err(err, errn, "directorio central dañado"); zip_close(z); return -1; }
        uint16_t flags = rd16(data + p + 8), method = rd16(data + p + 10);
        uint16_t nlen = rd16(data + p + 28), xlen = rd16(data + p + 30), clen = rd16(data + p + 32);
        if (p + 46 + nlen + xlen + clen > end) { set_err(err, errn, "directorio central dañado"); zip_close(z); return -1; }
        ZipEntry *e = &z->entries[z->count];
        size_t cn = nlen < sizeof e->name - 1 ? nlen : sizeof e->name - 1;
        memcpy(e->name, data + p + 46, cn);
        e->name[cn] = 0;
        e->crc = rd32(data + p + 16);
        e->csize = rd32(data + p + 20);
        e->usize = rd32(data + p + 24);
        e->method = method;
        e->header_offset = rd32(data + p + 42);
        p += 46u + nlen + xlen + clen;
        if (cn && e->name[cn - 1] == '/')
            continue;                                   /* carpeta */
        if (flags & 1)
            continue;                                   /* cifrada: no se puede leer, se ignora */
        z->count++;
    }
    return 0;
}

void zip_close(Zip *z) {
    free(z->entries);
    z->entries = NULL;
    z->count = 0;
}

int zip_extract(const Zip *z, size_t i, uint8_t *dst, char *err, size_t errn) {
    if (i >= z->count) { set_err(err, errn, "entrada inexistente"); return -1; }
    const ZipEntry *e = &z->entries[i];
    size_t lh = e->header_offset;
    if ((uint64_t)lh + 30 > z->size || rd32(z->data + lh) != 0x04034b50u) { set_err(err, errn, "cabecera local dañada"); return -1; }
    size_t start = lh + 30u + rd16(z->data + lh + 26) + rd16(z->data + lh + 28);
    if ((uint64_t)start + e->csize > z->size) { set_err(err, errn, "datos fuera del archivo"); return -1; }
    if (e->method == 0) {
        if (e->csize != e->usize) { set_err(err, errn, "entrada 'stored' con tamaños distintos"); return -1; }
        memcpy(dst, z->data + start, e->usize);
    } else if (e->method == 8) {
        if (sf2_inflate(z->data + start, e->csize, dst, e->usize) != 0) { set_err(err, errn, "datos comprimidos dañados"); return -1; }
    } else {
        set_err(err, errn, "método de compresión no soportado");
        return -1;
    }
    if (sf2_crc32(0, dst, e->usize) != e->crc) { set_err(err, errn, "CRC-32 del archivo no coincide con el del zip"); return -1; }
    return 0;
}
