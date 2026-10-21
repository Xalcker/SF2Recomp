# Revisiones de Street Fighter II: The World Warrior (CPS1)

Fuente: tabla `cps1_game_configs` de `src/mame/capcom/cps1_v.cpp` y lista
`GAME(...)` de `src/mame/capcom/cps1.cpp` (MAME, rama master, 2026-10-19).
Todas usan el mapeador de ROM gráfica STF29 y la misma placa a 10 MHz; lo que
cambia entre revisiones es el chip CPS-B (layout de registros y bits de
prioridad) y el código de programa (región, textos, correcciones).

**Revisión objetivo del proyecto: `sf2`, World 910522, CPS-B-11** (decisión D2).

| Set MAME | Región y fecha | CPS-B | Notas |
|---|---|---|---|
| **sf2** | **World 910522** | **CPS-B-11** | Set padre en MAME. La nuestra. |
| sf2em | World 910129 | CPS-B-17 | Primera revisión conocida |
| sf2ea / sf2en | World 910204 | CPS-B-17 | |
| sf2eb | World 910214 | CPS-B-17 | Era el set `sf2` en MAME 0.37b5 (mame2000) |
| sf2ee | World 910228 | CPS-B-18 | Necesita `init_sf2ee` en MAME |
| sf2ed | World 910318 | CPS-B-05 | |
| sf2ef | World 910411 | CPS-B-15 | |
| sf2ua | USA 910206 | CPS-B-17 | |
| sf2ub | USA 910214 | CPS-B-17 | |
| sf2ue | USA 910228 | CPS-B-18 | |
| sf2uc | USA 910306 | CPS-B-12 | |
| sf2ud | USA 910318 | CPS-B-05 | |
| sf2uf | USA 910411 | CPS-B-15 | |
| sf2ug / sf2uh / sf2ui | USA 910522 (Rev. G, H, I) | CPS-B-11 / 13 / 14 | Misma fecha que la nuestra; comparten parte de las ROMs de programa |
| sf2uk | USA 911101 | CPS-B-17 | |
| sf2um | USA 920312 | CPS-B-17 | Última revisión USA |
| sf2ja | Japan 910214 | CPS-B-17 | |
| sf2jc | Japan 910306 | CPS-B-12 | |
| sf2jf | Japan 910411 | CPS-B-15 | |
| sf2jh | Japan 910522 | CPS-B-13 | |
| sf2j | Japan 911210 | CPS-B-13 | |
| sf2jl | Japan 920312 | CPS-B-17 | Última revisión conocida del juego |

## Registros del CPS-B-11 (offsets desde 0x800140)

Según `#define CPS_B_11` en `cps1_v.cpp`:

| Registro | Offset | Dirección | Valor |
|---|---|---|---|
| ID del chip | 0x32 | 0x800172 | 0x0401 |
| Control de capas | 0x26 | 0x800166 | |
| Máscaras de prioridad (4) | 0x28, 0x2A, 0x2C, 0x2E | 0x800168 a 0x80016E | |
| Control de paleta | 0x30 | 0x800170 | |
| Bits de habilitación de capas (scroll 1, 2, 3) | | | 0x08, 0x10, 0x20 |

El handler de VBlank del juego lee 0x800172: es la comprobación del ID del
CPS-B-11 (protección de placa). Si el valor no es 0x0401 el juego no arranca
bien, así que el runtime debe devolverlo.

## Por qué World 910522

- Es el set padre en MAME y el que usan por defecto FBNeo, el core FPGA
  Arcade-CPS1 y la mayoría de colecciones: la referencia más probada.
- Revisión tardía de la línea World, en inglés y sin pantalla regional (las
  USA llevan la pantalla del FBI, las Japan textos en japonés).
- Ya está analizada y el hito 1 trabaja sobre ella.
- Cambiar de revisión más adelante es barato: se vuelve a pasar el
  recompilador y se cambia la fila del CPS-B en el runtime.
