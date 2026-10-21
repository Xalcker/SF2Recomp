#ifndef MUSASHI_GLUE_H
#define MUSASHI_GLUE_H
#include <stddef.h>
#include "mem.h"

extern Mem g_musashi_mem;        /* memoria propia de Musashi en paso cerrado */
extern Mem *g_musashi_mem_ptr;   /* memoria que usan los callbacks de Musashi (por defecto &g_musashi_mem) */
int recomp_musashi_int_ack(int level);
void musashi_check_irq(void);
int musashi_stopped(void);
int musashi_disasm(char *buf, size_t n, unsigned int pc);

#endif
