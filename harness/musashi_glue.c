/*
 * musashi_glue.c - conecta Musashi con la capa de memoria CPS1 del arnés.
 *
 * En paso cerrado (main.c) Musashi tiene su propia instancia Mem (g_musashi_mem) con RAM independiente,
 * para que las instrucciones read-modify-write no contaminen al otro lado.  En el arnés a ciegas
 * (blind.c) Musashi es la red de seguridad para código no recompilado y trabaja sobre la misma memoria
 * que el código generado: g_musashi_mem_ptr apunta a ella.
 */
#include <stdio.h>
#include "m68k.h"
#include "m68kcpu.h"
#include "mem.h"
#include "musashi_glue.h"

Mem g_musashi_mem;
Mem *g_musashi_mem_ptr = &g_musashi_mem;

unsigned int m68k_read_memory_8(unsigned int a)  { return mem_read8(g_musashi_mem_ptr, a); }
unsigned int m68k_read_memory_16(unsigned int a) { return mem_read16(g_musashi_mem_ptr, a); }
unsigned int m68k_read_memory_32(unsigned int a) { return mem_read32(g_musashi_mem_ptr, a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { mem_write8(g_musashi_mem_ptr, a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { mem_write16(g_musashi_mem_ptr, a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { mem_write32(g_musashi_mem_ptr, a, v); }

/* el desensamblador no debe registrar accesos ni tocar la E/S: lee solo ROM/RAM */
static unsigned int dasm_rd16(unsigned int a) {
    Mem *m = g_musashi_mem_ptr;
    a &= 0xffffff;
    if (a + 1 < ROM_SIZE) return (m->rom[a] << 8) | m->rom[a + 1];
    if (a >= WRAM_BASE) return (m->wram[a - WRAM_BASE] << 8) | m->wram[(a - WRAM_BASE + 1) & 0xffff];
    if (a >= GFXRAM_BASE && a + 1 < GFXRAM_BASE + GFXRAM_SIZE) return (m->gfxram[a - GFXRAM_BASE] << 8) | m->gfxram[a - GFXRAM_BASE + 1];
    return 0xffff;
}
unsigned int m68k_read_disassembler_8(unsigned int a)  { unsigned w = dasm_rd16(a & ~1u); return (a & 1) ? (w & 0xff) : (w >> 8); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return dasm_rd16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return (dasm_rd16(a) << 16) | dasm_rd16(a + 2); }

/* Reconocimiento de interrupción: autovector, y se retira la petición (nivel disparado). */
int recomp_musashi_int_ack(int level) {
    (void)level;
    m68k_set_irq(0);
    return M68K_INT_ACK_AUTOVECTOR;
}

/* Fuerza la comprobación de interrupciones pendientes sin ejecutar ninguna instrucción. */
void musashi_check_irq(void) {
    m68ki_check_interrupts();
}

int musashi_stopped(void) {
    return CPU_STOPPED != 0;
}

/* Desensambla la instrucción en `pc` en `buf` (tamaño n). */
int musashi_disasm(char *buf, size_t n, unsigned int pc) {
    char tmp[256];
    unsigned len = m68k_disassemble(tmp, pc, M68K_CPU_TYPE_68000);
    snprintf(buf, n, "%s", tmp);
    return (int)len;
}
