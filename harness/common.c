#include "common.h"
#include <stdlib.h>
#include <string.h>

uint32_t g_ring[TRACE_RING];
unsigned g_ring_pos;
static uint8_t g_covered[ROM_SIZE / 8];   /* cobertura dinámica: bit por dirección de instrucción ejecutada */
static uint64_t g_distinct;

void cov_mark(uint32_t pc) {
    if (pc < ROM_SIZE) {
        uint8_t bit = 1u << (pc & 7);
        if (!(g_covered[pc >> 3] & bit)) { g_covered[pc >> 3] |= bit; g_distinct++; }
    }
}

uint64_t cov_distinct(void) { return g_distinct; }

void cov_report(const char *path, unsigned insn_count) {
    printf("cobertura dinamica: %llu instrucciones distintas ejecutadas de %u estaticas (%.1f%%)\n",
           (unsigned long long)g_distinct, insn_count, 100.0 * (double)g_distinct / (double)insn_count);
    if (path) {
        FILE *f = fopen(path, "w");
        if (f) {
            for (uint32_t a = 0; a < ROM_SIZE; a += 2)
                if (g_covered[a >> 3] & (1u << (a & 7))) fprintf(f, "%06x\n", a);
            fclose(f);
            printf("direcciones ejecutadas escritas en %s\n", path);
        }
    }
}

void ring_push(uint32_t pc) { g_ring[g_ring_pos++ % TRACE_RING] = pc; }

void ring_dump(FILE *f, int (*dasm)(char *, size_t, unsigned)) {
    fprintf(f, "ultimas instrucciones (recompilado):\n");
    for (unsigned i = 0; i < TRACE_RING; i++) {
        uint32_t pc = g_ring[(g_ring_pos + i) % TRACE_RING];
        if (!pc) continue;
        if (dasm) {
            char buf[128];
            dasm(buf, sizeof buf, pc);
            fprintf(f, "  %06x: %s\n", pc, buf);
        } else {
            fprintf(f, "  %06x\n", pc);
        }
    }
}

void dump_regs(FILE *f, const char *tag, const uint32_t *d, const uint32_t *a, uint32_t pc, uint32_t sr) {
    fprintf(f, "%-9s pc=%06x sr=%04x\n", tag, pc, sr);
    fprintf(f, "          d0-d7:");
    for (int i = 0; i < 8; i++) fprintf(f, " %08x", d[i]);
    fprintf(f, "\n          a0-a7:");
    for (int i = 0; i < 8; i++) fprintf(f, " %08x", a[i]);
    fprintf(f, "\n");
}

uint8_t *load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    uint8_t *rom = calloc(ROM_SIZE, 1);
    size_t n = fread(rom, 1, ROM_SIZE, f);
    fclose(f);
    if (n != ROM_SIZE) { fprintf(stderr, "ROM de tamano inesperado (%zu bytes)\n", n); free(rom); return NULL; }
    return rom;
}
