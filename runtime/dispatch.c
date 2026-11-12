/* dispatch.c - tabla de despacho dirección -> función recompilada (rt_lookup), sobre la tabla que genera
 * recompiler/gen_c.py.  Va en la misma biblioteca que el código generado. */
#include "cpu.h"
#include "game.h"
#include "recomp_funcs.h"

/* variante cuyo programa se recompiló (gen_c.py --game) */
const GameDef *recomp_game(void) { return game_find(recomp_game_id); }

void rt_check_ram_code(M68K *c, uint32_t addr, const uint8_t *bytes, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t v = mem_read8(c->mem, addr + i);
        if (v != bytes[i])
            rt_fatal(c, "el codigo en RAM de %06x no es el que se recompilo (byte %06x: %02x, esperado %02x)", addr, addr + i, v, bytes[i]);
    }
}

RecompFn rt_lookup(uint32_t addr) {
    unsigned lo = 0, hi = recomp_table_len;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (recomp_table[mid].addr == addr)
            return recomp_table[mid].fn;
        if (recomp_table[mid].addr < addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}
