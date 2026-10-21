# 2026-10-20 · Mod widescreen · Fase 2: relleno del arte en los extremos de cámara

## Pregunta de partida

¿Se puede evitar que, con la cámara en un extremo, la vista ancha enseñe el borde del arte del escenario, sin cambiar el centro de
384 columnas ni la lógica del juego?

## Hallazgo que cambió el plan

El juego no transmite el mapa con la cámara en estos escenarios: carga el arte completo en el tilemap de SCROLL2 y SCROLL3 al empezar y
no lo cambia (en Honda las columnas con tiles visibles son idénticas con la cámara en 210 y en 384). Honda: SCROLL2 tiene tiles
visibles en 53 de las 64 columnas del anillo y SCROLL3 en 21 (columnas 10-30, 672 px); SCROLL3 con la cámara a la derecha llega a 1014 y
su arte acaba en 992. Así que el borde del arte se lee directamente de la RAM gráfica y no hace falta tocar el código del juego.

## Qué se hizo

- `runtime/gfx.[ch]`: `Gfx.blank[2]`, por código de 16 bits, 1 si el tile de SCROLL2/3 es transparente (fuera del mapeador o todos los pens a 15).
- `runtime/video.c`: `art_run_find` (tramo circular más largo de columnas sin tiles visibles), `fill_row_cols` (lo ensancha por filas con
  las columnas contiguas vacías en esa fila y asigna a cada columna la de arte más cercana, reflejada con `flipx`) y su uso en
  `tilemap_impl`. Solo se aplica a SCROLL2 y SCROLL3 y con la vista ancha; los segmentos que cruzan la ventana de la recreativa (crudas
  64-447) se parten para no dibujar dentro de ella.
- Primer intento por columnas globales: dejó un hueco en forma de cuña en Honda (el suelo, con rowscroll, acaba antes que el techo);
  la ampliación por filas lo resolvió.
- `tests/rom_tests.c`: `SF2_WIDE_FRAMES` para corridas largas de la prueba de identidad del centro.

## Resultados

| Comprobación | Resultado |
|---|---|
| Centro de 384 columnas frente al cuadro de 384, `ryu.txt` y `zangief.txt`, 42.000 cuadros cada una | 0 líneas distintas (28.947 y 28.873 cuadros anchos) |
| `make test` / `make compare-video` | 8 de 8 / 17 de 17 idénticos a MAME |
| Píxeles del color de fondo en las bandas de 64 px, máximo por escenario (antes → después) | Honda der. 8.996 → 412; Balrog der. 4.468 → 1.310; Vega der. 1.501 → 1.501; Ken izq. 314 → 1; Blanka der. 497 → 0 |
| Revisión visual de los extremos (Honda, Ken, Zangief, Chun-Li, Dhalsim, Balrog, Vega) | continuación plausible en todos; sin cortes del arte |
| Rendimiento (`--bench 12000 --bench-mode video`) | 3.464 cps a 384, 2.882 a 512 |

Los residuos de Balrog y Vega son en su mayoría negro del propio arte (el contador no distingue negro de fondo de negro de arte);
se revisaron a ojo. Queda alguna esquina menor en Honda (muesca de unos pocos píxeles en la banda derecha).

## Pendiente

Fase 3 (HUD pegado a los bordes) y fase 4 (lógica ancha), opcionales. Los bonus y el resto de pantallas siguen centrados en 384. No se probó
la versión Web ni se repitió `compare-coverage`.
