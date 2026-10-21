# 2026-10-20 · Mod widescreen · Fase 4: lógica ancha con interruptor

## Qué se hizo

- Análisis (desensamblado y trazas de RAM): los luchadores guardan la X en `+0x06` de su estructura (A6), la cámara de SCROLL2 en `0xFF8BD8`; la rutina `0x2D98` limita
  cada objeto a la pantalla (`0xBD8 + margen` a `0xBD8 + 0x180 - margen`) y `0x83650` sigue a los luchadores con topes por escenario (`0x9D0/0x9D2`).
- `recompiler/mod_patches.txt` + `--mods` en `gen_c.py` + `cmake/SF2Pipeline.cmake`: siete parches, todos con `c->mod_wide`.
- `runtime/cpu.h` (`mod_wide`), `machine.c` (el reinicio lo conserva), `sf2.[ch]` (`sf2_set_wide_logic`, `sf2_wide_logic_extra`), config `wide_logic`, `--wide-logic`, tecla F3.
- `tests/rom_tests.c`: `test_wide_logic` (apagada con 384 y con 512: mismo CRC de estado; encendida da 64 px extra y se puede apagar en caliente).

## Resultados

| Comprobación | Resultado |
|---|---|
| C generado frente al anterior | solo difieren las 7 líneas parcheadas |
| `playgen.py --verify` (14 secuencias, 603.050 cuadros) con el interruptor apagado | todos los CRC de estado iguales a la grabación; secuencias sin modificar |
| `make test` / `make compare-video` | 8 de 8 / 17 de 17 |
| Caminar contra las paredes (Ryu contra Blanka) | vanilla: P1 X 296-933, separación máxima 291; lógica ancha: P1 296-911, separación máxima 419, cámara 0-576 (antes 0-640) |
| Capturas | Ryu llega al borde izquierdo de la ventana ancha; la cámara encuadra dentro del arte |

## Pendiente / límites

No se probó a fondo la IA, los ataques que empujan contra la pared ni los proyectiles; no se repitió `compare-coverage` (apagada es idéntica por CRC).
La dificultad no está ajustada: con más espacio los combos de esquina duran menos. Falta que el usuario lo juegue.
