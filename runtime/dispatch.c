/* dispatch.c - tabla de despacho dirección -> función recompilada (rt_lookup), sobre la tabla que genera
 * recompiler/gen_c.py.  Va en la misma biblioteca que el código generado. */
#include "cpu.h"
#include "recomp_funcs.h"

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
