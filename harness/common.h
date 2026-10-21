/* common.h - piezas compartidas por los arneses: tabla de despacho, cobertura dinámica y anillo de traza. */
#ifndef HARNESS_COMMON_H
#define HARNESS_COMMON_H
#include <stdint.h>
#include <stdio.h>
#include "cpu.h"

#define TRACE_RING 64

extern uint32_t g_ring[TRACE_RING];
extern unsigned g_ring_pos;

void cov_mark(uint32_t pc);
void cov_report(const char *path, unsigned insn_count);   /* imprime el resumen y escribe la lista si path != NULL */
uint64_t cov_distinct(void);
void ring_push(uint32_t pc);
/* vuelca las últimas direcciones del anillo; `dasm` puede ser NULL (solo direcciones) */
void ring_dump(FILE *f, int (*dasm)(char *, size_t, unsigned));
void dump_regs(FILE *f, const char *tag, const uint32_t *d, const uint32_t *a, uint32_t pc, uint32_t sr);
uint8_t *load_rom(const char *path);

#endif
