# 2026-11-13 · Cajas de golpe en Street Fighter Zero (D75)

## Pregunta de partida

Tras el entrenamiento (D74), lo que faltaba de la página Entrenamiento en sfzch: las cajas de golpe y de daño. En SF2 se dibujan con un
formato descifrado de WW (`docs/HITBOXES.md`); en SFZ no se conocía.

## Cómo se encontró el formato

- Desensamblado de sfzch (`tools/analyze.py --disasm`, en el scratchpad). Se buscó quién resta la vida (+0x40 del luchador,
  `sub.w d3,0x40(a1)` en 0xE20E) y se subió a las rutinas que llaman: 0xDACA hace todas las colisiones del cuadro.
- **Empuje** (0xDB0A): pose en `0x20(a6)`, índice en el byte 0xC de la pose, tabla en `0x120(a6)`, registro de 4 bytes; X en 0x10, Y en
  0x14, espejo en 0xB. El registro es el de SF2: `x_off`, `y_off`, `rx`, `ry`.
- **Golpe de luchador** (0xDCC6): ataque con el byte 0xB de la pose y la tabla de `0x11C(a6)` (registros de 16 bytes); el defensor
  prueba tres cajas vulnerables, bytes 8, 9 y 0xA de su pose con las tablas de 0x110, 0x114 y 0x118. Solape en 0xECC2, igual que en SF2.
- **Proyectiles** (0xE744 contra proyectil, 0xE990 contra luchador): tabla en `0x50(a6)` con dos palabras de desplazamiento (empuje y
  ataque). Los recorre una lista de 0xFF307C que se rehace cada cuadro y en el VBlank está vacía; se usa la reserva de la que salen:
  8 objetos de 0x80 bytes en 0xFF9000 (0x138E4), tipo 8.
- **Cámara**: 0xFF8290, la que usa el juego para las paredes (`x - [0xFF8290]` contra 0x180); Y en 0xFF8294.

## Sonda

Script de `sf2py` en el scratchpad (no está en el repositorio): una partida de Ryu contra Rose que dibuja las cajas sobre la captura.

- Ryu y Rose parados al empezar el asalto: las cajas cubren a los dos, los pies llegan al suelo (Y = 240 - (y - cámara_y), como en SF2).
- Patada de Ryu: la caja roja sobre la pierna estirada; Rose en el aire con sus cajas.
- La bola de Rose no aparecía: el objeto (0xFF9000) tiene la tabla en la **RAM** (0xFF4360), no en la ROM. El lector acepta tablas en
  la RAM de trabajo.

## Qué se hizo

- `TrainingRam` gana `box_fmt` (`BOX_NONE`, `BOX_SF2`, `BOX_SFZ`), la cámara y la reserva de proyectiles; WW, CE y HF llevan los valores
  de su `ModRam`. `runtime/training.c`: `sf2_object_boxes` (el de antes) y `sfz_object_boxes`; `training_boxes` elige por formato y deja de
  leer `ModRam`. `sf2_training_boxes` ya no mira `has_mods`.
- `tests/rom_tests.c::test_training`: en cada cuadro de combate del versus automático cuenta las cajas de cada luchador y las que caen
  fuera de la pantalla; `test_no_mods` solo exige 0 cajas en variantes sin entrenamiento.

## Verificación

- `test_training`: cajas de los dos luchadores en los 3.778 cuadros de combate de sfzch (3.456 en WW) y ninguna fuera de la pantalla.
- Capturas de la aplicación (`--shot-png` con `show_hitboxes` y el versus automático): Chun-Li contra Ken, con el kikoken de Chun-Li en
  naranja sobre la bola.
- Probado jugando (tecla 9): visto bueno del usuario.
- Regresión (Windows): `ctest` en sf2, sf2ce, sf2hf y sfzch. Las cajas solo leen la RAM: con los mods apagados el juego no cambia.

## Pendiente

- Tercer luchador (0xFF8C00) con 0xFF2E38 distinto de 0: no se dibuja.
- El byte 0x13 de la pose (0xF en proyectiles) que se compara con 0x56 del defensor antes de golpear: sin descifrar.
- Agarres: como en SF2, sin caja propia encontrada.
