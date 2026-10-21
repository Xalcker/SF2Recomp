/* z80dis.c - desensamblado del programa de sonido (sf2_09.bin) con el desensamblador de runtime/z80.c.
 *
 *   build/z80dis sf2/sf2_09.bin [desde [hasta]]      (direcciones del Z80 en hexadecimal; banco 0 en 0x8000)
 */
#include <stdio.h>
#include <stdlib.h>
#include "z80.h"

static unsigned char rom[0x10000];

static uint8_t peek(void *ctx, uint16_t a) { (void)ctx; return rom[a]; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s sf2_09.bin [desde [hasta]]\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { perror(argv[1]); return 1; }
    fclose(f);
    unsigned from = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 16) : 0;
    unsigned to = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 16) : 0x8000;
    for (unsigned pc = from; pc < to;) {
        char buf[64];
        int n = z80_disasm((uint16_t)pc, peek, NULL, buf, sizeof buf);
        printf("%04x: ", pc);
        for (int i = 0; i < 4; i++) {
            if (i < n) printf("%02x ", rom[(pc + i) & 0xffff]);
            else printf("   ");
        }
        printf(" %s\n", buf);
        pc += n;
    }
    return 0;
}
