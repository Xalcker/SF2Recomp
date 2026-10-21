/*
 * zipread.h - lector de archivos zip en memoria (métodos "stored" y "deflate"), sin dependencias.
 *
 * Es lo justo para abrir sf2.zip de MAME: recorre el directorio central, extrae cada entrada con su propio
 * inflate (RFC 1951) y comprueba el CRC-32 y el tamaño del directorio central.  No admite zip64, cifrado ni
 * volúmenes múltiples.  Trabaja sobre un búfer que pone el anfitrión (archivo leído, o bytes recibidos del
 * navegador); todos los desplazamientos del archivo se validan, porque el contenido no es de fiar.
 */
#ifndef RUNTIME_ZIPREAD_H
#define RUNTIME_ZIPREAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZipEntry {
    char name[256];            /* ruta dentro del zip (truncada a 255 caracteres) */
    uint32_t crc, csize, usize;
    uint16_t method;           /* 0 = stored, 8 = deflate */
    uint32_t header_offset;    /* cabecera local */
} ZipEntry;

typedef struct Zip {
    const uint8_t *data;       /* el búfer no se copia: debe sobrevivir al Zip */
    size_t size;
    ZipEntry *entries;
    size_t count;
} Zip;

/* abre el zip; 0 si va bien, -1 con un mensaje corto en `err` */
int zip_open(Zip *z, const uint8_t *data, size_t size, char *err, size_t errn);
void zip_close(Zip *z);
/* extrae la entrada i en dst (usize bytes) y comprueba tamaño y CRC; 0 si va bien, -1 con mensaje */
int zip_extract(const Zip *z, size_t i, uint8_t *dst, char *err, size_t errn);

/* inflate de un flujo deflate crudo que debe producir exactamente outlen bytes; 0 si va bien */
int sf2_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);

#ifdef __cplusplus
}
#endif

#endif
