/* crc32.h - CRC-32 (IEEE 802.3, el de zip y de los catálogos de ROMs de MAME). */
#ifndef RUNTIME_CRC32_H
#define RUNTIME_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC acumulado: empieza con crc = 0 y devuelve el valor tras `n` bytes más */
uint32_t sf2_crc32(uint32_t crc, const void *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif
