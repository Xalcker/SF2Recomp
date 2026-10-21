/* dasmcheck: compara la longitud de cada instrucción decodificada por tools/m68k.py (lista
 * build/insns.txt: "addr len texto") con la que calcula el desensamblador de Musashi. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"

extern const unsigned char *g_dasm_rom;
extern size_t g_dasm_rom_size;

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "uso: dasmcheck rom.bin insns.txt\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    unsigned char *rom = malloc(0x100000);
    size_t n = fread(rom, 1, 0x100000, f);
    fclose(f);
    g_dasm_rom = rom;
    g_dasm_rom_size = n;
    FILE *l = fopen(argv[2], "r");
    if (!l) { perror(argv[2]); return 1; }
    char line[512], buf[256];
    unsigned total = 0, bad = 0;
    while (fgets(line, sizeof line, l)) {
        unsigned addr, len;
        char text[256] = "";
        if (sscanf(line, "%x %u %255[^\n]", &addr, &len, text) < 2) continue;
        unsigned mlen = m68k_disassemble(buf, addr, M68K_CPU_TYPE_68000);
        total++;
        if (mlen != len) {
            bad++;
            if (bad <= 40)
                printf("%06x: python len %u '%s'  musashi len %u '%s'\n", addr, len, text, mlen, buf);
        }
    }
    printf("%u instrucciones comparadas, %u discrepancias de longitud\n", total, bad);
    return bad ? 1 : 0;
}
