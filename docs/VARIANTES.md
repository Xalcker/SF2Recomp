# Otras variantes de SF2: Champion Edition y Hyper Fighting

Objetivo: recompilar también *Street Fighter II': Champion Edition* (set `sf2ce` de MAME, World 920513) y *Street Fighter II':
Hyper Fighting* (set `sf2hf`, World 921209) con el mismo método que *The World Warrior* (`sf2`): código C generado en la máquina del
usuario a partir de sus ROMs, idéntico a MAME 0.289 cuadro a cuadro, y con los mods apagados por omisión. Como primer juego fuera de
la serie SF2 está además *Street Fighter Zero* en la versión de CPS Changer (`sfzch`), sin mods (ver al final).

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
| 1 | CE en vanilla: generar con `-DSF2_GAME=sf2ce`, buscar el manejador de la IRQ2, el punto de muestra y el contador de cuadros (`program_known`), llenar `tools/hints_sf2ce.txt` hasta 0 fallos de despacho y `compare-*` idénticos a MAME | **jugable e idéntica a MAME** en las partidas probadas (2026-11-12, D64-D66); falta cobertura: secuencias propias de CE (`playgen.py`, 12 personajes) y `compare-coverage` |
| 2 | HF en vanilla: catálogo `sf2hf`, el resto como CE | **jugable e idéntica a MAME** en las partidas probadas (2026-11-12, D67); falta cobertura, como en CE |
| 3 | Mods por variante: direcciones de `recompiler/mod_patches.txt`, `runtime/training.c`, la cola de sonido del estéreo, la RAM de la escena ancha... | **hecha** (2026-11-12, D68) salvo las herramientas de desarrollo (solo WW); límites en la bitácora `2026-11-12-variantes-fase-3` |
| 4 | *Street Fighter Zero* (CPS Changer, `sfzch`) en vanilla: programa de 2 MiB, gráficos de 8 MiB, entradas del Changer | **jugable** (2026-11-13, D71); sin comparar con MAME (no hay MAME en la máquina donde se hizo) |

## Cómo se elige la variante

- Al compilar: `make GAME=sf2ce ROMDIR=/ruta/sf2ce.zip` o `cmake -DSF2_GAME=sf2ce -DSF2_ROM_DIR=...` (por omisión `sf2`). Un árbol de
  compilación mueve una sola variante: el C generado es de un programa concreto.
- `tools/interleave.py --game` monta la imagen de programa; `gen_c.py --game` la comprueba (tamaño) y escribe `recomp_game_id` en el C
  generado. El núcleo toma el descriptor con `recomp_game()` y la aplicación busca `<set>.zip` (`sf2ce.zip`) y carga ese catálogo.
- Las pistas del análisis son `tools/hints.txt` (WW) y `tools/hints_<set>.txt` (las demás); los parches de los mods, `recompiler/mod_patches.txt`
  (WW) y `recompiler/mod_patches_<set>.txt` (D68).
- Las tres variantes comparten la carpeta de preferencias y `sf2recomp.ini`; los estados guardados llevan el set en el nombre
  (`sf2hf_slot0.sta`), así que cada una tiene sus ranuras. Si pones `[paths] rom` en el `.ini`, vale para una sola variante: a las otras pásales
  la ruta en la línea de comandos.

## Qué pasa hoy con HF

```bash
make GAME=sf2hf BUILD=build-hf ROMDIR=sf2hf            # o la ruta a tu sf2hf.zip
build-hf/sf2 sf2hf
```

Idéntica a MAME en 20.000 cuadros de atracción, 12.000 de `inputs_sf2.txt` y 8.000 de audio. Pistas en `tools/hints_sf2hf.txt` (las mismas
familias que en CE, más `not_vector`); el punto de muestra es la escritura de 0x800122 en 0x1D264, tras el `movem` del manejador. Al
compararla apareció un caso del reloj de la IRQ que WW y CE no tenían (D67). Bitácora `2026-11-12-variantes-fase-2`.

## Qué pasa hoy con CE

```bash
make GAME=sf2ce BUILD=build-ce ROMDIR=sf2ce            # o la ruta a tu sf2ce.zip
build-ce/sf2 sf2ce                                     # o build-ce/sf2 /ruta/sf2ce.zip
make compare-mame compare-sound-cmds GAME=sf2ce BUILD=build-ce ROMDIR=sf2ce
```

Se juega en vanilla y es idéntica a MAME en todo lo probado: 20.000 cuadros de atracción y 12.000 de la partida de `inputs_sf2.txt`
(estado, registros, comandos de sonido, capturas) y 8.000 de audio (Z80 al ciclo, YM2151, OKI y WAV). Lo que tiene de distinto respecto a
WW y cómo se resolvió está en la bitácora `2026-11-12-variantes-fase-1`: arranque por `trap #14`, rutinas de protección montadas en la
RAM gráfica (D64), punto de muestra con una escritura y la regla del reloj de la IRQ a 12 MHz (D65). Desde la fase 3 (D68) tiene los mods
de WW salvo las herramientas de desarrollo. Falta recorrer el resto del juego (cobertura dinámica ~32 %): pueden aparecer más fallos de despacho, que se arreglan
con pistas en `tools/hints_sf2ce.txt` como dice el propio mensaje de error.

## Street Fighter Zero (CPS Changer)

```bash
cmake -S . -B build-sfzch -DSF2_GAME=sfzch -DSF2_ROM_DIR=sfzch ...   # o make GAME=sfzch BUILD=build-sfzch ROMDIR=sfzch
build-sfzch/sf2 sfzch
```

Set `sfzch` de MAME (Japan 951020): la versión doméstica para el CPS Changer, que es un CPS-1 en una consola. Lo que cambia respecto a CE/HF:

| | `sfzch` |
|---|---|
| Máquina | `cps1_12MHz`, 12 MHz / 201.216 ciclos por cuadro |
| CPS-B | `CPS_B_21_DEF` sin puerto IN2 (`{"sfzch", CPS_B_21_DEF, mapper_SFZ63B}`: 0x800176 lee 0xFFFF) |
| Mapeador gráfico | `mapper_SFZ63B`: 4 bancos lineales de 0x8000 unidades, todos los tipos en todos |
| Programa | **2 MiB**, cuatro ROMs de palabras de 512 KiB |
| Gráficos | **8 MiB**, 16 ROMs (`GFX_ROM_SIZE_MAX`; el núcleo reserva lo que pide cada variante) |
| Entradas | IN0: MK (bits 0-1), Select (2-3), Start (4-5), HK (6-7); IN1: direcciones, LP/MP/HP y LK en los bits 7 y 15. Sin bloqueo de monedas |

Las entradas del juego son las de siempre: `cps1_apply_inputs` las reordena al formato del Changer (`GameDef.changer_inputs`), así que el
teclado, el mando, `--inputs` y las secuencias usan los mismos nombres. La tecla de moneda hace de Select. Punto de muestra: la
escritura de 0x800100 en 0x1076, primer acceso de E/S tras el `movem` del manejador de la IRQ2 (0x1068); contador de cuadros 0xFF8018.
Sin mods (no hay `recompiler/mod_patches_sfzch.txt`): pantalla ancha, entrenamiento y estéreo se rechazan como en cualquier variante sin
mods (D66). Pistas en `tools/hints_sfzch.txt`. Bitácora `2026-11-13-sfzch`.

Probado en Windows (GCC 16.2): `ctest` 8/8, 40.000 cuadros de atracción sin fallos de despacho, una partida (start, selección, combate)
con los seis botones distinguibles, y sonido (YM2151 y OKI). **No se ha comparado con MAME**. Queda un conflicto en el análisis
(`023b64<-023b60`, ver D71).
