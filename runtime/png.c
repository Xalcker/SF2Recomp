/* png.c - codificador PNG mínimo en memoria (ver png.h): RGB de 8 bits, bloques zlib sin comprimir. */
#include "png.h"
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* añade un fragmento (longitud, tipo, datos, CRC) al búfer de salida */
static uint8_t *put_chunk(uint8_t *o, const char *type, const uint8_t *data, size_t n) {
    put32(o, (uint32_t)n);
    memcpy(o + 4, type, 4);
    if (n) memcpy(o + 8, data, n);
    uint32_t c = crc_update(0xffffffffu, o + 4, 4 + n) ^ 0xffffffffu;
    put32(o + 8 + n, c);
    return o + 12 + n;
}

int png_encode_rgb(const uint32_t *frame, int w, int h, uint8_t **png, size_t *len) {
    if (!crc_ready) crc_init();
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   /* 8 bits, RGB, sin entrelazar */
    /* datos crudos: byte de filtro 0 + 3 bytes por píxel por fila */
    size_t rowlen = 1 + (size_t)w * 3, rawlen = rowlen * (size_t)h;
    uint8_t *raw = malloc(rawlen);
    if (!raw) return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * rowlen;
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t p = frame[(size_t)y * (size_t)w + (size_t)x];
            r[1 + x * 3] = (uint8_t)(p >> 16); r[2 + x * 3] = (uint8_t)(p >> 8); r[3 + x * 3] = (uint8_t)p;
        }
    }
    /* zlib con bloques "stored" (sin compresión): cabecera 78 01, bloques de hasta 65535 bytes, adler32 */
    size_t nblocks = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblocks * 5 + 4;
    uint8_t *z = malloc(zlen);
    if (!z) { free(raw); return -1; }
    size_t zi = 0, ri = 0;
    z[zi++] = 0x78; z[zi++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    while (ri < rawlen || nblocks == 0) {
        size_t n = rawlen - ri;
        if (n > 65535) n = 65535;
        int final = (ri + n == rawlen);
        z[zi++] = (uint8_t)final;
        z[zi++] = (uint8_t)n; z[zi++] = (uint8_t)(n >> 8);
        z[zi++] = (uint8_t)~n; z[zi++] = (uint8_t)(~n >> 8);
        memcpy(z + zi, raw + ri, n);
        zi += n; ri += n;
        if (final) break;
    }
    put32(z + zi, (b << 16) | a);
    zi += 4;
    size_t total = 8 + (12 + 13) + (12 + zi) + 12;
    uint8_t *out = malloc(total), *o = out;
    if (!out) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    memcpy(o, sig, 8); o += 8;
    o = put_chunk(o, "IHDR", ihdr, 13);
    o = put_chunk(o, "IDAT", z, zi);
    o = put_chunk(o, "IEND", NULL, 0);
    free(raw);
    free(z);
    *png = out;
    *len = (size_t)(o - out);
    return 0;
}
