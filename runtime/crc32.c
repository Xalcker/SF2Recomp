#include "crc32.h"

/* tabla por nibbles (16 entradas): más lenta que la de 256 pero sin inicialización ni estado global */
static const uint32_t nib[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
    0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu, 0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

uint32_t sf2_crc32(uint32_t crc, const void *data, size_t n) {
    const uint8_t *p = data;
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ nib[crc & 15];
        crc = (crc >> 4) ^ nib[crc & 15];
    }
    return ~crc;
}
