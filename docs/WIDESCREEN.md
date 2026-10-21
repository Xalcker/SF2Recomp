# Widescreen real (más píxeles a los lados, sin estirar)

Fecha: 2026-10-20. Estado: **fases 0 a 4 implementadas (2026-10-20, D53-D58); la lógica ancha (fase 4) es un interruptor aparte que cambia el juego.**
Evidencia y comandos en `bitacora/2026-10-20-mod-widescreen-viabilidad.md`.

## 1. Qué significa y qué ancho

La recreativa muestra 384x224 píxeles en una pantalla 4:3, es decir, con píxeles más altos que anchos (relación 7:9).
Si se mantiene esa forma de píxel y se pide una pantalla 16:9, caben exactamente **512 píxeles de ancho**
(512 · 7/9 = 398 → 398/224 = 16:9). Con la misma altura y la misma escala que hoy en modo `aspect = 4:3`, la ventana
sale 16:9 y el juego enseña 64 píxeles más por cada lado. Nada se estira.

512 es además el techo real de este chip: el contador horizontal de la placa y las coordenadas X de los sprites son de
9 bits (0-511), y SCROLL1 mide 512. Más ancho (21:9, 672 px) obligaría a duplicar sprites y a inventar fondo, así que
queda fuera. Se propone `video_width` configurable entre 384 y 512 (múltiplos de 16), por omisión 512 cuando se active el
modo y 384 con el modo apagado.

## 2. Qué pasa hoy si solo se ensancha el renderizador (prototipo)

Se compiló una copia de `runtime/video.c` con `VIDEO_W = 512` y `VIDEO_RAW_X0 = 0` (en el scratchpad, sin tocar el
proyecto) y se corrió la secuencia `ryu.txt` (los 8 escenarios, bonus, selección y final), volcando cuadros y barriendo
los registros de scroll cada 10 cuadros. Resultados:

| Qué | Resultado |
|---|---|
| Sprites y personajes | Bien: nunca aparecen sprites fantasma a los lados en combate. |
| Fondos con la cámara a media pista | Bien en los 8 escenarios: los tilemaps ya traen contenido más allá de los 384. |
| Fondos con la cámara en un extremo | **Mal** en varios: a la derecha de Honda, Balrog y Vega y a la izquierda de Ken (barco) se ve el color de fondo o tiles cortados, con dientes por el rowscroll de SCROLL2. En Blanka solo la derecha. |
| Fases de bonus | **Mal**: solo hay arte en los 384 originales (14.336 píxeles vacíos en cada banda). |
| Selección de personaje, pantallas fijas | **Mal**: mapa y fondos compuestos para 384; bandas con restos de tiles. |
| HUD (barras, marcador, tiempo) | Funciona, pero queda centrado en los 384 originales, con las barras lejos de los bordes. |

Conclusión: el cambio de ancho del renderizador es trivial; el trabajo está en **qué dibujar donde el juego no dibujó nada**.

## 3. Por qué faltan datos: el juego transmite el mapa por columnas

La cámara lógica está en RAM: SCROLL1 `0xFF8036`, SCROLL2 `0xFF803A`, SCROLL3 `0xFF803E` (A5 = 0xFF8000; la X de los
registros CPS-A es esa menos 64, y se copia con un cuadro de retraso en `0x1736`: `0xFF8060/64/68`). El motor no tiene
los tilemaps llenos: por cada capa mantiene una ventana deslizante de columnas y escribe una columna nueva cada vez que
la cámara cruza un múltiplo de 16 px (rutinas en `0x083FE8`, `0x084016`, `0x08404A`, `0x08416E`, `0x0841EC`, `0x084264`,
`0x08447E`; la posición de cámara de cada capa está en estructuras tipo `0xBD2(A5)`, `0xC52(A5)` con X en +6 y el puntero
al mapa del escenario en +0x12). La ventana de SCROLL2 cubre del orden de 144 px de margen por lado, así que en la
mayor parte del recorrido el margen de 64 px que pedimos ya está cargado. Lo que no existe es lo que hay más allá del
**arte del escenario**: el recorrido de la cámara es de 192 px (SCROLL2 de 192 a 384), o sea 576 px de arte en total, y
una vista de 512 px en un extremo asoma 64 px fuera de ese arte.

## 4. Plan por fases

Cada fase se verifica sola, cierra con una entrada de bitácora y no puede romper lo anterior. **Regla del proyecto:** con el
modo apagado el juego sigue siendo idéntico a MAME (`make compare-video`, `make compare-coverage`, huellas de `ctest`).

### Fase 0 · Ancho de vídeo como parámetro (render puro)
- `Video` pasa de `VIDEO_W` constante a `v->w` (máximo 512), con buffers dimensionados al máximo; `VIDEO_RAW_X0` pasa a
  `64 - (w - 384) / 2`. `tilemap_impl` y `draw_sprite_tile` ya recortan con la anchura, así que el cambio es mecánico.
- Fachada: `sf2_set_video_width()` y `sf2_frame_width()`; `SF2_SCREEN_W` queda como el ancho base (384).
- Anfitrión: textura, captura PNG y `tests/trace.c` leen el ancho; `app/main.c` ajusta `aspect = 4:3` para que
  512 salga 16:9 (la altura de la ventana no cambia) y la versión Web cambia el tamaño del lienzo.
- **Hecho cuando:** con `w = 384` las 14 secuencias y `make compare-video` dan exactamente lo de hoy; y una prueba nueva
  verifica que con `w = 512` el recorte central de 384 es **idéntico píxel a píxel** al render de 384 desde el mismo
  estado (capas, sprites y prioridades).

### Fase 1 · Decidir qué pantallas se ensanchan
- Ensanchar solo en combate y sus presentaciones (`0xFF8001` = 2, `0xFF8005` = 4 y las fases de ronda/victoria; la lista
  exacta se levanta con un barrido de las 14 secuencias). El resto (título, selección, mapa, bonus mientras no tengan arte,
  continuar, final, test) se dibuja en los 384 originales centrados, con barras laterales del color de fondo
  (`VIDEO_BACKDROP_PEN`) o negras.
- El cambio de ancho ocurre en un corte de escena (cuando cambia la fase), nunca a mitad de un fundido.
- **Hecho cuando:** ninguna de las capturas de las 14 secuencias muestra restos de tiles ni sprites a los lados.

### Fase 2 · Relleno del arte en los extremos (HECHA, D54)
Resultado: se eligió la estrategia A, con un hallazgo que la simplificó: el arte completo está cargado en el tilemap y es estático, así
que el borde se detecta en la RAM gráfica (no hace falta el motor de columnas de §3) y se refleja fuera de la ventana original. Honda
pasó de 8.996 a 412 píxeles sin pintar en la banda derecha, y el resto de escenarios quedó en decenas o en falsos positivos (negro del
propio arte). Detalle y medidas en `bitacora/2026-10-20-mod-widescreen-fase-2.md`. Lo que sigue es el plan original:

### (plan original de la fase 2)
Dos estrategias; se prueba la A y la B solo si hace falta:
- **A. Extensión en el renderizador (lógica del juego intacta).** Para cada capa se averigua el rango de columnas con arte
  válido (de las estructuras del motor, o mirando qué columnas escribió por última vez) y fuera de él se repite la
  última columna válida o se refleja el borde. Hay que respetar el rowscroll de SCROLL2 (cada fila se desplaza distinto, de
  ahí los dientes de Honda) y la prioridad por tile. Los fondos con elementos fijos en el borde (valla de Vega, farola de
  Balrog) quizá pidan reflejo en vez de repetición.
- **B. Acortar el recorrido de la cámara** de 192 a 64 px parcheando sus topes (el código se recompila; los topes hay que
  localizarlos en las rutinas de seguimiento de `0x083xxx`-`0x085xxx`, todavía sin identificar) para que la vista de 512 nunca salga del arte. Cambia lo que se ve del escenario
  y obliga a revisar las paredes de los luchadores, así que solo si A queda feo.
- **Hecho cuando:** un barrido automático (cámara en cada extremo de los 8 escenarios y de los bonus) no deja píxeles de
  fondo sin pintar a los lados, y una revisión visual de cada extremo da el visto bueno.

### Fase 3 · HUD (HECHA, D55, opción `hud = edges`)
Corrección al plan: el marcador son sprites en ranuras fijas de la tabla de objetos, no filas de SCROLL1; se desplazan ±64 px por ranura y
lado. Detalle en `bitacora/2026-10-20-mod-widescreen-fase-3.md`. Plan original:

### (plan original de la fase 3)
- Por omisión se queda centrado (cero riesgo). Opción `hud = edges`: en las filas del HUD, la mitad izquierda de SCROLL1
  se desplaza -64 px y la derecha +64 px, de modo que las barras de vida lleguen a los bordes; el KO y el tiempo se
  quedan en el centro. Es un desplazamiento por rango de columnas en el propio renderizador.

### Fase 4 · Lógica ancha (opcional, desactivada por omisión)
- Los luchadores y los proyectiles están limitados a los 384 originales (no pueden salir de la pantalla y los
  proyectiles se descartan al salir). Con la vista de 512 se podría ampliar ese límite, pero cambia el juego: más
  distancia posible, rincones distintos, y deja de coincidir con MAME. Si se hace, es un mod aparte con su interruptor,
  con las direcciones de los topes sacadas con scripts Lua de MAME (según la regla de `MEJORAS.md`).
- Para la primera entrega **no se toca la lógica**: lo que se gana a los lados es escenario y lo que ya asomaba.

### Fase 5 · Configuración, pruebas y documentación
- `config.ini`: `widescreen = off|on`, `video_width`, `hud`; atajo de teclado para alternar; respeta `scale`, `aspect` y
  `integer_scale`.
- Pruebas `ctest`: igualdad del recorte central (fase 0), ancho 384 idéntico a hoy, barrido de bordes (fase 2) y
  estados guardados entre anchos.
- Rendimiento: +33 % de píxeles (hoy ~0,2 ms de vídeo por cuadro en macOS, de 13.761 a 3.669 cps); se mide y se anota.
- Bitácora, decisión en `DECISIONES.md` (D53 en adelante), README de `runtime/` y `app/`, y una línea en `MEJORAS.md`.

## 5. Orden y esfuerzo estimado

| Fase | Esfuerzo | Riesgo |
|---|---|---|
| 0 Ancho parametrizado | medio día | bajo |
| 1 Pantallas ensanchables | medio día | bajo |
| 2 Relleno de los extremos | 1-2 días | **alto** (es el núcleo del mod) |
| 3 HUD en los bordes | medio día | bajo |
| 4 Lógica ancha | 2 días o más | alto, y rompe la equivalencia con MAME |
| 5 Configuración y pruebas | 1 día | bajo |

Las fases 0 y 1 dan ya una versión usable (combate ancho, pantallas fijas centradas, con la cámara en los extremos
sin arreglar); la 2 es la que la hace presentable.

## 6. Decisiones que conviene confirmar

1. **Ancho 512 (16:9 con píxeles de recreativa)** como objetivo único, con `video_width` ajustable hacia abajo. Recomendado.
2. **Primera entrega sin tocar la lógica del juego** (fase 4 fuera). Recomendado.
3. **Pantallas fuera de combate centradas en 384** con barras, en vez de intentar ensancharlas una a una.
4. HUD centrado por omisión, `edges` como opción.
