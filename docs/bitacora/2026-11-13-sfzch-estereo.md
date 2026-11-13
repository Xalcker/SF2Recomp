# 2026-11-13 · Sonido estéreo en Street Fighter Zero (D73)

## Pregunta de partida

El usuario preguntó qué mods se podían llevar a sfzch. Revisión: ninguno se porta como en CE y HF (allí cada instrucción parcheada de WW se
encontraba por el código que la rodea; SFZ es otro motor). Experimento sin commit: forzando 768 px, el escenario sí sigue a los lados, pero
el marcador es una capa de tiles que se repite en los bordes, y los objetos que entran por los lados necesitarían los parches de D62/D69.
El HFR no lee la RAM del juego. Se eligió empezar por el **sonido estéreo**.

## Cómo se encontró todo

- **Cola de sonido.** La rutina que la vacía ya se conocía de D72: 0x11322 lee el índice `-0x49E4(a5)`, la cola está en `-0x59E6(a5)`
  (a5 = 0xFF8000) y 0x11334/0x1133C escriben los latches 1 y 2. Buscando los accesos a esa cola: 0x11350/0x1135C encola (`move.w
  d1,0x0(a4,d0.w)` en **0x11364**, índice de escritura `-0x49E6(a5)`, 256 palabras con `andi.w #$1fe`) y detrás encola `0xFF00`.
- **Quién pide cada sonido.** Script de Lua en MAME (scratchpad, no está en el repositorio): tap de escritura sobre 0xFF261A-0xFF2819 que anota
  PC, comando, a6 y 32 bytes desde a6. En combate (partida de prueba con hadokens): a6 = 0xFF8400 y 0xFF8800 (los luchadores) y 0xFF9000
  (golpes y proyectiles); en menús, objetos de 0xFFA100-0xFFA600 con coordenadas de pantalla. X de mundo en `0x10(a6)`, Y en `0x14(a6)`
  (0x28 en el suelo).
- **Cámara.** Volcados de RAM en seis cuadros de combate: las palabras que dejan a los dos luchadores dentro de los 384 px y cambian entre
  cuadros. 0xFF8290 (con copias en 0xFF802E, 0xFF82A0 y 0xFF82A8) deja el punto medio de los luchadores en 192,5 en los seis: es la cámara
  principal; 0xFF8310 es una capa de fondo con paralaje.

## Qué se hizo

- `StereoRam` en el descriptor (`runtime/game.h`, `game.c`), aparte de `ModRam` y `has_mods`; `mod_sound_tag` y `machine.c` lo leen.
  `snd_x`/`snd_ok` pasan de 64 a 256 ranuras. `sf2_has_stereo()` en la fachada; la aplicación y `sf2_set_stereo` lo usan.
- `recompiler/mod_patches_sfzch.txt` con el parche de 0x11364; `"mods": True` en `tools/games.py`.
- **Primer intento: sin paneo en el combate.** Solo salían voces paneadas en la demostración. El «sin comando» de sfzch es la palabra
  0xFF00 (latch 1 = 0x00), y la placa solo ignoraba el 0xFF de SF2: un cuadro después de cada sonido llegaba un «comando» sin posición y el
  Z80 aún no había arrancado la frase. `machine.c` lee la palabra de la ranura que se saca y, si es el «sin comando» de la variante, se lo
  dice a la placa (`SOUND_POS_NOCMD`), que deja la posición como estaba.
- **Fuera del combate.** En la selección el luchador 1 pide el sonido de confirmar con X = 0x30 y la cámara a 0 (iría a la izquierda).
  Byte que distingue: 0x4 de la estructura del luchador, 2 con el luchador en juego (partida, combates de la demostración y el de la intro
  con el motor del juego) y 0 en título, selección e ilustraciones. Los bytes de modo (0xFF8001/5/9) no sirven: la demostración y la
  intro comparten valores.
- `test_no_mods` (`tests/rom_tests.c`): en una variante con `StereoRam` exige que el estéreo se active, que haya muestras con L ≠ R y que
  `sf2_game_crc` y `sf2_state_crc` sean los mismos con y sin él.

## Verificación

- Partida de hadokens (3.600 cuadros, `sf2_blind --stereo --wav`): 586.253 de 2.949.121 muestras con L ≠ R, todas en el combate de la
  partida (cuadros 2.680-3.880) y en el de la intro (600-1.490); energía L/R 1,23 con Ryu a la izquierda lanzando hadokens.
- `ctest` 8/8 en sf2, sf2ce, sf2hf y sfzch; en sfzch, `test_no_mods` cuenta 670.233 muestras con L ≠ R en 3.000 cuadros de
  `inputs_sf2.txt` y la partida no cambia.
- Con el mod apagado, contra MAME 0.289 (12.000 cuadros, las cuatro variantes): estado, 17 de 17 capturas, comandos de sonido y audio
  (Z80 al ciclo, YM2151, OKI, mezcla) idénticos; diferencia de instante de los latches 0.

## Pendiente

- Oírlo jugando (`--stereo` o menú ▸ Sonido).
- Los objetos de los bonus y de otras pantallas de SFZ no se han revisado; quedan al centro si no están en el rango 0xFF9000-0xFF9FFF.
