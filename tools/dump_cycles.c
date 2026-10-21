/* Vuelca la tabla de ciclos base del 68000 de Musashi (65536 bytes) para que gen_c.py
 * pueda estimar el tiempo en el modo autónomo (sin Musashi). */
#include <stdio.h>
#include "m68k.h"
#include "m68kcpu.h"
#include "m68kops.h"

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "uso: dump_cycles salida.bin\n"); return 1; }
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    for (int i = 0; i < 0x10000; i++)
        fputc(m68ki_cycles[0][i], f);
    fclose(f);
    return 0;
}
