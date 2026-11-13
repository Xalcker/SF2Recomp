# 2026-11-12 · Pantalla ancha: la franja sin dibujar del borde izquierdo (D69, incidencia #3)

## Pregunta de partida

Incidencia #3: «en los formatos anchos queda una franja muy delgada en el lado izquierdo de la pantalla donde no se dibujan píxeles». Había
que ver en qué modos y en qué capa. Sospechas apuntadas en la incidencia: el relleno de los extremos (`art_run_find`, `fill_row_cols`), el
recorte de `draw_sprite_tile` con `rx0` negativo o el rowscroll en la primera columna de tiles.

Se trabajó con *Hyper Fighting* (`sf2hf`) en Windows, la única variante con ROMs en esta máquina.

## Medidas

Herramientas en el scratchpad (no forman parte del repositorio): una sonda sobre la fachada `sf2.h` que cuenta, cuadro a cuadro, los tiles
de sprite de la tabla latcheada según su X respecto a los bordes de la vista, y calcula el CRC de las 384 columnas centrales y de
`sf2_game_crc` de todos los cuadros; y un lector de PNG para comparar capturas por columnas.

- **Capas de scroll: bien.** 51 capturas de la demostración a 512 px (cada 150 cuadros, dos combates): ninguna columna negra ni sin
  rellenar en los 12 px de cada borde. El relleno de D54 y `fill_narrow_holes` no dejan hueco.
- **Sprites: ahí está.** Tiles de 16 px cuya X cae entre «borde izquierdo − 15» y «borde izquierdo − 1», es decir, que cruzan el borde:

  | Ancho | Modo | Cuadros | Cruzan el borde izquierdo | Tocan el derecho por dentro |
  |---|---|---:|---:|---:|
  | 384 | original, demostración | 9.000 | 4.024 | 3.646 |
  | 512 | original, demostración | 9.000 | **0** | 70 |
  | 512 | original, `inputs_sf2.txt` | 6.000 | **0** | 3.761 |
  | 512 | lógica ancha, `inputs_sf2.txt` | 6.000 | **0** | 259 |
  | 688 | stage completo, `inputs_sf2.txt` | 6.000 | **0** | 0 |
  | 768 | stage completo, `inputs_sf2.txt` | 6.000 | **0** | 0 |

  A 384 px el juego construye miles; en cualquier modo ancho, ninguno.
- **Causa.** El constructor de sprites descarta los tiles con X negativa o ≥ 512 (`cmpi.w #$200,d4` sin signo, ocho veces; en CE y HF hay
  además un segundo constructor). En la recreativa la ventana empieza en la X cruda 64 y un tile que asoma por la izquierda tiene X de 49 a
  63: nunca es negativa. La vista ancha empieza en `64 − (ancho − 384) / 2`: 0 a 512 px, −88 a 688, −128 a 768. Los tiles que cruzan ese
  borde tienen X negativa (a 512) o menor que `−mod_bx` (a más de 512, donde `mod_bx = (ancho − 512) / 2` coincide justo con el borde y deja
  fuera los 15 px anteriores). A la derecha no pasa: la X de 9 bits llega a 511 y `512 + mod_bx` es el borde derecho.

## Qué se hizo

- `runtime/sf2.c`: `sprite_margin(ancho) = max(0, (ancho − 384) / 2 − 48)`; lo usan `shadow_frame` (máquina sombra, lógica original) y
  `apply_wide_logic` (lógica ancha y stage completo) para `c->mod_bx`, y `Video.xsigned` se enciende cuando el margen del ancho pedido es
  mayor que 0 (desde 482 px; antes, solo por encima de 512). Los parches de `recompiler/mod_patches*.txt` no cambian.
- `tests/rom_tests.c` (`test_widescreen`): cuenta e informa los tiles que cruzan el borde izquierdo; en HF exige que haya alguno.
- Comentarios de `runtime/video.h`, `runtime/cpu.h` y `recompiler/mod_patches.txt`; `docs/WIDESCREEN.md` y D69.

## Resultado

| Ancho | Modo | Cruzan el borde izquierdo (antes → después) | CRC del centro | CRC del juego |
|---|---|---:|---|---|
| 512 | original, demostración (9.000) | 0 → 212 | igual que a 384 | igual que a 384 |
| 688 | original, demostración (9.000) | 0 → 0 (no hay objetos tan lejos) | igual que a 384 | igual que a 384 |
| 512 | original, `inputs_sf2.txt` (6.000) | 0 → 2.208 | sin cambio | sin cambio |
| 512 | lógica ancha (6.000) | 0 → 2.573 | cambia | cambia |
| 688 | stage completo (6.000) | 0 → 2.439 | cambia | cambia |
| 768 | stage completo (6.000) | 0 → 2.653 | cambia | cambia |

- Capturas antes y después (512 px, cuadros 1800, 2000 y 2800 de `inputs_sf2.txt`): solo difieren las columnas 0 a 7; en el escenario de
  Honda el cristal de la bañera, que es un sprite, quedaba cortado a 5 px del borde y ahora llega hasta él. A 688 con stage completo, lo
  mismo en las columnas 0 a 7 del cuadro 2500. Con `--png-frames` del juego (otro punto de muestra), el cuadro 2000 difiere en las
  columnas 4 a 12: la franja puede medir hasta 15 px, el ancho de un tile menos un píxel.
- Con la lógica original la partida no cambia (el arreglo va en la máquina sombra). Con la lógica ancha y el stage completo el CRC del juego
  cambia: los tiles de más se construyen en la propia partida y cuestan ciclos del 68000 (lo mismo que ya documentaba D62). Esos dos modos
  ya cambiaban el juego; lo que se pierde es la reproducción cuadro a cuadro de partidas grabadas en ellos con la versión anterior.
- `ctest` en `build-hf` (Windows, GCC 16.2): 8/8.

## Pendiente

- ~~Repetir las medidas en WW y en CE~~: hecho el 2026-11-13 (ver «Seguimiento»).
- `make compare-video` y `playgen.py --verify` en macOS, que con los mods apagados no deberían notar nada.
- Que el usuario lo vea jugando en 512, 688 y 768.
- No se ha tocado la incidencia #4 (elefantes de Dhalsim en el stage completo).

## Seguimiento (2026-11-13)

Con las ROMs de WW y CE ya en la máquina, `main` tras la fusión (PR #7) se compiló en Windows (GCC 16.2) con `SF2_GAME=sf2` y `sf2ce`:
`ctest` 8/8 en las dos. `test_widescreen` (512 px, `inputs_sf2.txt`, 3.000 cuadros) informa 336 tiles que cruzan el borde izquierdo en WW
y 660 en CE, y 0 líneas del centro de 384 columnas distintas en las dos: el margen funciona igual en las tres variantes y, con la lógica
original, no cambia la imagen central.
