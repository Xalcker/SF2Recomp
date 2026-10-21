# recompiler/

Generador de C a partir del análisis del programa 68000 (`tools/analyze.py`). Produce una función C por función del
juego más la tabla de despacho dirección → función. El C generado deriva de las ROMs y no se versiona (decisión D5):
desde el hito 5 va a `<build>/generated` (no al árbol de fuentes).

```bash
python3 recompiler/gen_c.py build/rom.bin build/generated --hints tools/hints.txt \
        --cycles build/cycles68000.bin --parts 16 --report build/coverage.txt
```

Normalmente lo ejecuta CMake como paso de la compilación (`cmake/SF2Pipeline.cmake`: `interleave.py` → `dump_cycles` →
`gen_c.py`) sobre las ROMs de `SF2_ROM_DIR` (D36); en compilaciones cruzadas lo hace el sub-proyecto nativo
`recompiler/host` (desde el hito 6 se entra en él en cada compilación, `BUILD_ALWAYS 1`, para que un cambio del recompilador
llegue también a `build-win` y `build-web`). `make -j8` lo lanza a través del envoltorio.

## Qué genera

- `mod_patches.txt` (D58): parches de los mods de lógica, con `c->mod_wide = 0` iguales al original; `--mods` de `gen_c.py`.
- `recomp_funcs.h`: prototipos `void f_XXXXXX(M68K *c);`, la macro `TAILDISPATCH` y las
  declaraciones de la tabla (`recomp_table`, `recomp_table_len`, `recomp_reset_pc`,
  `recomp_insn_count`) y, desde el hito 5, `recomp_rom_crc32`: el CRC-32 de la imagen de programa con la que se
  generó, que el juego comprueba al cargar las ROMs (D38).
- `recomp_NN.c`: las funciones, repartidas en N archivos equilibrados para compilar en paralelo.
- `recomp_table.c`: `{ dirección, función }` ordenada por dirección para todas las etiquetas
  despachables (entradas, destinos de salto, puntos de retorno tras `jsr`/`trap`).

## Forma del código generado

```c
void f_000426(M68K *c)
{
    switch (c->pc) {                 /* entrada por cualquier etiqueta del cuerpo */
    case 0x426u: goto L_000426;
    case 0x7ccu: goto L_0007CC;
    default: rt_fatal(c, "entrada invalida %06x en f_000426", c->pc);
    }
L_000426:
    INSN(0x426u); CYC(4); /* lea $00042e(pc),a4 */
    { uint32_t ea0 = 0x42eu; c->a[4] = ea0; }
    INSN(0x42au); CYC(4); /* bra.w $0007cc */
    goto L_0007CC;
    ...
```

| Instrucción 68000 | C generado |
|---|---|
| `jsr X` / `bsr X` | `push32(c, ret); c->pc = X; f_X(c); if (c->pc != ret) return;` |
| `rts` | `c->pc = pop32(c); return;` (el llamador comprueba la dirección) |
| `jmp X` misma función | `goto L_X;` |
| `jmp X` otra función | `c->pc = X; __attribute__((musttail)) return f_X(c);` |
| `jmp (ea)` | `switch (destino) { case …: goto …; default: TAILDISPATCH; }` |
| `jsr (ea)` | `push32(ret); c->pc = ea; dispatch_call(c); if (c->pc != ret) return;` |
| `trap #n`, `chk`, div/0, `trapv`, `illegal` | `exception(c, vec, pc); dispatch_call(c); if (c->pc != ret) return;` |
| `rte` / `rtr` | sacan SR (o CCR) y PC y retornan |
| resto | bloque `{ … }` con EA, lectura, `op_*()` de `runtime/cpu.h`, escritura |

Si un `rts` devuelve una dirección distinta de la esperada (el juego cambia de tarea, descarta
marcos con `addq.l #6,a7`, salta con `pea X ; rts`…), los niveles C retornan uno a uno hasta que
alguno coincida o hasta el trampolín de `harness/main.c`, que busca `c->pc` en la tabla y entra en
la función propietaria por su `switch`.  Así la pila C refleja la pila 68000 en el caso normal y se
resincroniza sola en los demás.

## Idiomas que resuelve el análisis (resumen; detalle en `docs/bitacora/…-desensamblador.md`)

Tablas de offsets `.w` y de punteros `.l` PC-relativas de Capcom, tablas de código de paso fijo,
retorno por registro `lea ret(pc),a4 ; bra sub … jmp (a4)`, `pea X(pc)`, y los argumentos de
`trap #n` (o de un envoltorio `trap ; rts`) que son PC de tarea, constantes o leídos de una tabla.

## Destinos indirectos sin resolver estáticamente

Los únicos 9 (`jmp (An)` en 0x6EE, 0x73A, 0x750, 0x766, 0x7AE, 0x7CA, 0x826, 0xAB6, 0x12818) son
retornos por registro cuyos destinos ya son entradas; se resuelven por la tabla de despacho.
Cuando el arnés encuentre un destino que no esté en la tabla termina con

```
ERROR FATAL en pc=XXXXXX: destino indirecto sin resolver XXXXXX
```

o `retorno/salto a una direccion sin etiqueta`; se añade `entry XXXXXX` (o `label XXXXXX`) a
`tools/hints.txt` y se recompila.  `table_end BASE FIN` acota a mano una tabla sobreextendida.

## Reloj (hito 6, D47)

Cada instrucción lleva `CYC(n)` con el coste del 68000 de MAME 0.289: la tabla de Musashi (`--cycles`, de `dump_cycles`) más
`mame_fix()` para las clases en que difieren, y los ajustes dependientes de los datos (Bcc/DBcc, desplazamientos, MOVEM,
MULS con `cyc_muls_68k`, operaciones de bit con bit < 16). Además `c->ke = 0x....u;` (y otro valor en la rama tomada de un
Bcc.b o un Scc cierto): el punto en que el microcódigo decide si atiende una interrupción, de `recompiler/m68000_ack.txt`
(`--ack`, generada por `tools/mame_m68000_ack.py`). `--clock musashi` vuelve al modelo de los hitos 1-5 (D16).

## Correcciones manuales (`tools/hints.txt`, D12, D49)

`entry ADDR`, `label ADDR`, `table_end BASE FIN` y, desde el hito 6, `inline_text RUTINA`: las llamadas a esa rutina vuelven
detrás de un texto en línea (fila, columna, atributo, caracteres hasta 0, `/` para otra posición), que no se decodifica. El
informe del análisis (`build/coverage.txt`) dice cuántos destinos de rama o caída no son una instrucción decodificada; tiene
que ser 0.

## Cifras actuales

Hito 6: 43.036 instrucciones en 1.339 funciones (17.812 duplicadas por bloques compartidos), 11.325
etiquetas despachables, 160.448 líneas de C en 16 archivos, 0 destinos sin decodificar. (Hito 1: 43.216
instrucciones, de las que 356 eran datos tomados por código.)
Cobertura estática del ROM de 1 MB: 15,42 % código, 0,70 % tablas (20,56 % y 0,93 % de los 768 KB
que contienen código).
