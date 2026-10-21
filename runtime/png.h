/* png.h - codificador PNG mínimo en memoria, sin dependencias (D25): RGB de 8 bits con bloques zlib sin
 * comprimir, CRC-32 y Adler-32 propios.  Los píxeles son 0x00RRGGBB, fila a fila.  Devuelve en *png un
 * búfer de malloc (a liberar con free) y su longitud; 0 si va bien, -1 sin memoria.  Escribirlo en un archivo
 * es cosa del anfitrión. */
#ifndef RUNTIME_PNG_H
#define RUNTIME_PNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int png_encode_rgb(const uint32_t *frame, int w, int h, uint8_t **png, size_t *len);

#ifdef __cplusplus
}
#endif

#endif
