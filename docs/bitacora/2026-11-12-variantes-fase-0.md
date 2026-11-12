# 2026-11-12 · Variantes de SF2, fase 0: núcleo para varias variantes

## Pregunta de partida

El usuario preguntó si se podía dar soporte a *Champion Edition* y *Turbo / Hyper Fighting*. Revisado el driver de MAME 0.289
(`src/mame/capcom/cps1.cpp`, `cps1_v.cpp`): las dos van a 12 MHz (`cps1_12MHz`), con `CPS_B_21_DEF` y `mapper_S9263B`, y 1,5 MiB de
programa en ROMs de palabras. El usuario aprobó empezar por la fase 0 de CE: preparar el núcleo sin tocar el comportamiento de WW.
Plan completo en `docs/VARIANTES.md`.

## Qué se hizo

- `runtime/game.h` / `game.c` (nuevo, en `sf2_base`): `GameDef` con reloj, ciclos por cuadro, tamaño del programa, `CpsbConfig` (valores
  crudos de la tabla de MAME), `GfxMapper`, catálogo de ROMs y hechos del programa (manejador de la IRQ2, punto de muestra, contador de
  cuadros, `program_known`, `has_mods`). Descriptores `game_sf2` (todo lo que antes eran constantes) y `game_sf2ce` (hardware y ROMs;
  programa sin analizar).
- `mem.c`: `Mem.game` y `Mem.rom_size`; la lectura del CPS-B reproduce `cps1_cps_b_r` con la configuración (mismo orden y la misma división
  entera, con la que el CPS-B-11 lee en 0x800140 `reg0 * reg0`, que ya hacíamos); control de paleta por la configuración.
- `video.c`: prioridades, control de capas y bits de habilitación desde `CpsbConfig` (CE usa 0x02/0x04/0x08).
- `gfx.c`: `gfx_map(mapper, tipo, código)`; `gfx_init` toma el catálogo y el mapeador de `rs->game`.
- `romset.c`: `RomSet.game`, `romset_init(rs, game)`, candidatos por variante, `romset_complete`, `romset_index`, ROMs de programa de
  palabras (`ROMLANE_WORD`, `ROM_LOAD16_WORD_SWAP`); el informe nombra la variante.
- `sound.c`: `Cps1Sound.m68k_hz` (de `game->cpu_hz`) en la conversión de ciclos 68000 → Z80.
- `cps1.c` / `machine.c`: ciclos por cuadro, contador de cuadros y punto de muestra desde el descriptor (`Machine.sample_pc`; sin
  punto de muestra si el programa no está analizado). `machine_init(mc, game, rom, frame_cycles)`.
- `sf2.c`: `sf2_game()`, `sf2_frame_hz()` (`SF2_FRAME_HZ` pasa a llamarla); `sf2_create` comprueba que las ROMs son de la variante del
  ejecutable y se niega a arrancar una variante sin analizar.
- `training.c`: límites de lectura de la ROM por `rom_size` (el mod sigue siendo solo de WW).
- Código generado: `gen_c.py --game` comprueba el tamaño de la imagen, escribe `recomp_game_id` y, en una variante sin mods, no aplica
  `mod_patches.txt`; `runtime/dispatch.c` define `recomp_game()`. `tools/analyze.py` toma el fin del programa de la imagen
  (`self.rom_end`). `tools/games.py` (nuevo, espejo de `game.c`) y `tools/interleave.py --game`. `tools/hints_sf2ce.txt` vacío.
- CMake: `SF2_GAME` (por omisión `sf2`), `<build>/gen_game.txt` como sello (como `gen_mods.txt`), pistas por variante, y el sub-proyecto
  de las builds cruzadas la recibe. `make GAME=...`.
- Aplicación: busca `<set>.zip` y los mensajes nombran la variante; `platform/hostio.c` busca en carpetas los nombres del catálogo de la
  variante. Arneses, `render_dump`, `sf2py` y pruebas, adaptados.
- Pruebas: `test_romset` recorre los dos catálogos (CRC y nombres distintos, programa completo, ciclos por cuadro = reloj × 512 × 262 /
  8 MHz); `test_cpsb` (nuevo): lecturas del CPS-B-11 y del CPS-B-21, programa de 1,5 MiB, STF29 = S9263B en los 65.536 códigos de los
  cuatro tipos, montaje de ROMs de palabras.

## Verificación

- Línea base con los binarios de antes del cambio (`build/` del 2026-11-04) y los nuevos, misma partida (`tools/inputs_sf2.txt`, 12.000
  cuadros): `sf2_blind --summary --detail`, WAV, `--sound-cmds`, 5 PNG y `sf2_trace` (CRC de estado, vídeo y audio cada 1.000 cuadros):
  **todo idéntico byte a byte**. `build/rom.bin` idéntica; el C generado solo añade `recomp_game_id`.
- `make test`: 8/8 (`unit`: 981 comprobaciones).
- `python3 tools/playgen.py --verify tools/sequences/*.txt`: las 14 secuencias reproducen su CRC de estado y no cambian.
- `make web`: compila (el sub-proyecto nativo genera con `--game sf2`). No se probó en el navegador.
- Errores: `interleave.py --game sf2ce sf2.zip` lista las 3 ROMs de CE que faltan; `gen_c.py --game sf2ce` con la imagen de WW se niega
  por el tamaño. `gen_c.py --game sf2ce` sobre la imagen de WW rellenada a 1,5 MiB genera sin errores, sin parches de mods y con
  `recomp_game_id = "sf2ce"`.
- No se ejecutaron `make compare-*` contra MAME: con la salida idéntica a la de antes, siguen valiendo los resultados del hito 6.

## Pendiente

Fase 1 (`docs/VARIANTES.md`): hace falta `sf2ce.zip`. No se ha ejecutado nada con las ROMs reales de CE; Windows no se recompiló.
