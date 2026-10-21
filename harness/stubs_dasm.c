/* Callbacks de memoria mínimos para herramientas que solo usan el desensamblador / tablas de Musashi. */
#include <stdint.h>
#include <stddef.h>
#include "m68k.h"

const uint8_t *g_dasm_rom;
size_t g_dasm_rom_size;

static unsigned rd16(unsigned a) {
    a &= 0xffffff;
    if (g_dasm_rom && a + 1 < g_dasm_rom_size) return (g_dasm_rom[a] << 8) | g_dasm_rom[a + 1];
    return 0xffff;
}
unsigned int m68k_read_memory_8(unsigned int a)  { unsigned w = rd16(a & ~1u); return (a & 1) ? (w & 0xff) : (w >> 8); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return (rd16(a) << 16) | rd16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { (void)a; (void)v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_32(unsigned int a, unsigned int v) { (void)a; (void)v; }
unsigned int m68k_read_disassembler_8(unsigned int a)  { return m68k_read_memory_8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
int recomp_musashi_int_ack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }
