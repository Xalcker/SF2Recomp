# Otras variantes de SF2: Champion Edition y Hyper Fighting

Objetivo: recompilar también *Street Fighter II': Champion Edition* (set `sf2ce` de MAME, World 920513) y *Street Fighter II':
Hyper Fighting* (set `sf2hf`, World 921209) con el mismo método que *The World Warrior* (`sf2`): código C generado en la máquina del
usuario a partir de sus ROMs, idéntico a MAME 0.289 cuadro a cuadro, y con los mods apagados por omisión.

## Qué cambia respecto a WW (MAME 0.289)

| | `sf2` (WW) | `sf2ce` (CE) | `sf2hf` (HF) |
|---|---|---|---|
| Máquina de MAME | `cps1_10MHz` | `cps1_12MHz` | `cps1_12MHz` |
| Reloj del 68000 / ciclos por cuadro | 10 MHz / 167.680 | **12 MHz / 201.216** | 12 MHz / 201.216 |
| CPS-B | `CPS_B_11` (ID 0x0401 en 0x800172) | **`CPS_B_21_DEF`** (0x800172 lee 0xFFFF; multiplicador reg0 × reg1 en 0x800144/146) | `CPS_B_21_DEF` |
| Bits de capas (SCROLL1/2/3) | 0x08 / 0x10 / 0x20 | **0x02 / 0x04 / 0x08** | 0x02 / 0x04 / 0x08 |
| Mapeador gráfico | `mapper_STF29` | `mapper_S9263B` (mismos rangos) | `mapper_S9263B` |
| Programa | 1 MiB, cuatro pares de ROMs de bytes | **1,5 MiB**, tres ROMs de palabras de 512 KiB | 1,5 MiB |
| Gráficos | 6 MiB, ROMs `sf2-*m` | 6 MiB, ROMs `s92-*m` | casi las de CE |
| Sonido | Z80 + YM2151 + OKI | igual (otro programa del Z80, mismas muestras del OKI) | igual |

Las prioridades (0x800168-0x80016E), el control de capas (0x800166) y el de paleta (0x800170) están en los mismos registros.

## Fases

| Fase | Qué | Estado |
|---|---|---|
| 0 | Núcleo para varias variantes sin cambiar WW: descriptor `GameDef` (`runtime/game.h`), reloj, CPS-B, mapeador y catálogo de ROMs por variante, `SF2_GAME` en CMake, `gen_c.py --game` | **hecha** (2026-11-12, D63) |
| 1 | CE en vanilla: generar con `-DSF2_GAME=sf2ce`, buscar el manejador de la IRQ2, el punto de muestra y el contador de cuadros (`program_known`), llenar `tools/hints_sf2ce.txt` hasta 0 fallos de despacho, secuencias de `playgen.py` (12 personajes) y `compare-*` idénticos a MAME | pendiente (necesita `sf2ce.zip`) |
| 2 | HF en vanilla: catálogo `sf2hf`, el resto como CE | pendiente |
| 3 | Mods por variante: direcciones de `recompiler/mod_patches.txt`, `runtime/training.c`, la cola de sonido del estéreo, la RAM de la escena ancha... | pendiente |

## Cómo se elige la variante

- Al compilar: `make GAME=sf2ce ROMDIR=/ruta/sf2ce.zip` o `cmake -DSF2_GAME=sf2ce -DSF2_ROM_DIR=...` (por omisión `sf2`). Un árbol de
  compilación mueve una sola variante: el C generado es de un programa concreto.
- `tools/interleave.py --game` monta la imagen de programa; `gen_c.py --game` la comprueba (tamaño) y escribe `recomp_game_id` en el C
  generado. El núcleo toma el descriptor con `recomp_game()` y la aplicación busca `<set>.zip` (`sf2ce.zip`) y carga ese catálogo.
- Las pistas del análisis son `tools/hints.txt` (WW) y `tools/hints_<set>.txt` (las demás). `mod_patches.txt` solo se aplica a WW
  (`has_mods` / `"mods"` en `tools/games.py`).

## Qué pasa hoy con CE

El catálogo, el reloj y el CPS-B de CE ya están en el núcleo y probados con pruebas unitarias, pero su programa no está analizado
(`program_known = 0`): `sf2_create` se niega a arrancarlo con un mensaje que remite a este documento. La recompilación de una imagen de
1,5 MiB se probó con una imagen sintética (la de WW rellenada con ceros); con las ROMs reales de CE no se ha ejecutado nada todavía.
