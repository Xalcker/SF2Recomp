# Widescreen real (más píxeles a los lados, sin estirar)

Estado: **implementado** (fases 0 a 4, 2026-10-20, D53-D58; HUD y pantallas fijas D55-D57; stage completo en `ULTRAWIDE.md`). Se usa desde el menú **M**, **F9**, `--widescreen` o
`widescreen = true`. Resumen de todos los mods en `MODS.md`. Evidencia y comandos de cada paso en `bitacora/2026-10-20-mod-widescreen-*.md`.

## 1. Qué hace y cómo se usa

La recreativa muestra 384x224 píxeles en una pantalla 4:3, es decir, con píxeles más altos que anchos (relación 7:9). Si se mantiene esa forma de píxel y se pide una pantalla 16:9, caben
**512 píxeles de ancho** (512 · 7/9 = 398 → 398/224 = 16:9): el juego enseña 64 píxeles más por cada lado y nada se estira. 512 es además el techo natural del chip: la X de los sprites es
de 9 bits y SCROLL1 mide 512; los anchos mayores (hasta 800) están pensados para el stage completo, que además cambia el constructor de sprites (`ULTRAWIDE.md`).

| Opción | Qué es | Cambia el juego |
|---|---|:---:|
| `widescreen` (F9, `--widescreen`) | cuadro de `wide_width` píxeles en el combate, los bonus y la demostración | no |
| `wide_width` (menú ▸ Ancho del cuadro: 512, 576, 640, 688, 768, 800) | ancho del cuadro, par, de 384 a 800; con `aspect = 4:3` y 512 la ventana es 16:9 | no |
| `hud = edges` (`--hud-edges`) | el marcador se pega a los bordes (por omisión `center`) | no |
| `wide_logic` (F3, `--wide-logic`) | los luchadores usan todo el ancho y la cámara sigue dentro del arte | **sí** |

El **ajuste rápido** del menú (Original, Widescreen 16:9 = 512 + `hud = edges` + `aspect = 4:3`, y los dos ultraanchos) cambia varias de estas cosas a la vez.
Apagado (`widescreen = false`, 384) el juego es idéntico a MAME; los dos primeros interruptores solo tocan el dibujo y no entran en el estado guardado.

## 2. Cómo está hecho

### Ancho del cuadro (fase 0 y 1, D53)
`Video.w` (`sf2_set_video_width`, `sf2_frame_width`) sustituye al ancho fijo; `SF2_SCREEN_W = 384` es el ancho base. El ancho del cuadro **no cambia con la escena**: las escenas compuestas
para 384 (título, mapa, selección, versus, diálogo, final) se dibujan en 384 centradas con barras, y solo el combate (`video_scene_wide`, por la RAM del juego) usa todo el ancho; así la
textura y la ventana no se redimensionan. El criterio de escena ancha cuenta también la demostración del modo atracción (D56). Invariante: en toda escena las 384 columnas centrales
del cuadro ancho son idénticas al cuadro de 384 (`roms_and_states`, `test_widescreen`).

### Relleno del arte en los extremos (fase 2, D54)
El juego carga el arte completo del escenario en SCROLL2 y SCROLL3 (anillo de 64 columnas) y no lo cambia al mover la cámara, así que el borde del arte se lee de la RAM gráfica (no hace
falta el motor de columnas). En un extremo, la vista ancha asoma por ese hueco y se rellena **reflejando** la columna de arte más cercana, solo fuera de la ventana original (coordenadas
crudas 64-447), por lo que el centro sigue idéntico. Honda pasó de 8.996 a 412 píxeles sin pintar en la banda derecha; el resto de escenarios quedó en decenas de píxeles o en falsos
positivos. SCROLL1 (marcador) no se toca. Se descartaron mover las capas (desincroniza los sprites) y repetir la última columna (rayas de 64 px).

### HUD en los bordes (fase 3, D55, D56)
El marcador son sprites en ranuras fijas de la tabla de objetos (0-13 puntuación, 14-25 tiempo y récord, 26-43 nombres, 44-63 barras de vida, 64-71 mensajes, 72-75 KO); con
`hud = edges` cada ranura se mueve ±64 px según caiga a un lado u otro del centro. Los luchadores no se tocan, y los sprites con y ≥ 80 se ignoran para no partir el «INSERT COIN» de la
demostración. Invariante: con la opción activa el cuadro no cambia fuera de las 48 primeras filas.

### Pantallas fijas y bonus (D57)
Selección, mapa, versus, final y créditos se dibujan solo en la ventana original; las barras laterales toman el color del fondo de la pantalla (el del borde en la fila de arriba). Los bonus se
ensanchan como un combate, con el relleno de D54.

### Sprites en las bandas añadidas (D62, 2026-11-04)
El juego decide qué objetos dibuja con ventanas «¿está a la vista?» pensadas para 384 px (`0x28C2`, `0x28FA`, `0x2932` y las de escenario `0x47D4`-`0x4824`:
el origen del objeto entre −64 y 448 respecto de la cámara, con márgenes según su tamaño). En las bandas añadidas los objetos aparecían y desaparecían de golpe: la
lámpara de Honda, las piedras y las bicicletas de Chun-Li, los barriles de Ken. Los parches de `mod_patches.txt` ensanchan esas ventanas con `c->mod_view` (píxeles extra
por lado de la vista) y el constructor de sprites admite X fuera de 0-511 con `c->mod_bx` cuando la vista lo necesita (desde 482 px, ver «Borde izquierdo» más abajo; entonces
el renderizador lee la X con signo).
Aplicarlo en la partida la cambia (dibujar más objetos cuesta ciclos del 68000 y el juego se ralentiza de otra forma: la X del rival divergía contra Dhalsim), así que
**con la lógica original lo hace una máquina sombra** (`shadow_frame` en `runtime/sf2.c`): antes de cada cuadro se copia la placa principal sin el sonido, la copia corre
dos cuadros con los parches y su segundo latch de la tabla de sprites sustituye al real en el VBlank siguiente (la tabla que se latchea en un VBlank se construyó en el
cuadro anterior). La partida real no lleva parches y es idéntica a la original (RAM de trabajo igual en todos los cuadros de `ryu.txt`, `ken.txt` a 512 y `chunli.txt` a
640); la copia no arrastra diferencias porque parte cada cuadro del estado real. Cuesta correr el núcleo tres veces en las escenas anchas (con vídeo, 3.306 → 1.807
cuadros por segundo en macOS, 30 veces el tiempo real). Con la lógica ancha o el stage completo, que ya cambian el juego, los parches van en la propia partida.

Además: los sprites de bloque (varios tiles en una entrada) que pasan de la X 511 ya no reaparecen por la izquierda en los cuadros ensanchados (en la placa daban la vuelta
fuera de la ventana de 384), y el criterio de escena ancha cuenta la subfase 0x0E (un cuadro hacia el recuento) y el paso de la demostración a su victoria (modo 0x0C fase 0
con luchadores), que dejaban las bandas vacías un cuadro.

### Borde izquierdo: tiles de sprite con X negativa (D69, 2026-11-12, incidencia #3)
En todos los modos anchos quedaba una franja de hasta 15 px pegada al borde izquierdo donde no se dibujaban sprites (los objetos del escenario y, con la lógica ancha o el
stage completo, los luchadores contra la pared aparecían cortados); en el derecho no. El constructor de sprites del juego descarta los tiles con X negativa o de 512 en
adelante. En la recreativa no se nota: la ventana empieza en la X cruda 64 y un tile de 16 px que asoma por la izquierda tiene X de 49 a 63. La vista ancha empieza en
`64 − (ancho − 384) / 2` (0 a 512 px, −88 a 688): los tiles que cruzan ese borde tienen X negativa y el juego no los construía, y el margen `c->mod_bx` de D62 se había
calculado para el borde derecho (`(ancho − 512) / 2`, 0 a 512 px). Ahora `sprite_margin` (`runtime/sf2.c`) da `(ancho − 384) / 2 − 48`, lo justo para que el constructor
acepte X desde 16 px antes del borde izquierdo (16 a 512 px, 104 a 688, 144 a 768), y vale 0 hasta 480 px, donde el juego original ya basta. Con margen, la X se guarda
con signo y `Video.xsigned` se enciende (antes solo por encima de 512). Medido en *Hyper Fighting* con `inputs_sf2.txt` a 512 px: de 0 a 2.208 tiles cruzando el borde
izquierdo en 6.000 cuadros, y las capturas comparadas solo cambian en las primeras columnas (como mucho 15: el ancho de un tile menos un píxel). Con la lógica original el arreglo va en la máquina sombra y la partida no cambia (mismo
CRC del centro de 384 columnas y del estado del juego que a 384 px); con la lógica ancha o el stage completo va en la propia partida, que construye algún tile más por
cuadro y por tanto no reproduce cuadro a cuadro las partidas grabadas con la versión anterior en esos dos modos.

### Lógica ancha (fase 4, D58)
Las paredes de los luchadores y los topes de la cámara usan 384 píxeles fijos en el código del juego. `recompiler/mod_patches.txt` cambia, por dirección, siete instrucciones del C
generado (`0x2D98` pared de un objeto contra la cámara, `0x83650` seguimiento de la cámara) para que lean `c->mod_wide` (píxeles extra por lado; 0 = original). Con el interruptor apagado
las 14 secuencias dan los mismos CRC de estado (`playgen.py --verify`); encendido, un luchador llega 64 px más lejos de cada pared (separación máxima 291 → 419 en la prueba de caminar).

## 3. Qué se midió antes de implementarlo (viabilidad)

Se compiló una copia del renderizador con ancho 512 y se corrió `ryu.txt` (los 8 escenarios, bonus, selección y final):

| Qué | Resultado |
|---|---|
| Sprites y personajes | Bien: nunca aparecen sprites fantasma a los lados en combate. |
| Fondos con la cámara a media pista | Bien: los tilemaps ya traen contenido más allá de los 384. |
| Fondos con la cámara en un extremo | **Mal** en varios (Honda, Balrog, Vega a la derecha; Ken a la izquierda): color de fondo o tiles cortados, con dientes por el rowscroll de SCROLL2. → fase 2 |
| Fases de bonus | **Mal**: solo hay arte en los 384 originales. → D54/D57 |
| Selección, pantallas fijas | **Mal**: mapa y fondos compuestos para 384. → D57 |
| HUD | Funciona, pero centrado en los 384 originales. → fase 3 |

La cámara lógica está en RAM: SCROLL1 `0xFF8036`, SCROLL2 `0xFF803A`, SCROLL3 `0xFF803E` (la X de los registros CPS-A es esa menos 64 y se copia con un cuadro de retraso en `0x1736`).
El motor mantiene una ventana deslizante de columnas por capa (rutinas `0x083FE8`-`0x08447E`); el recorrido de la cámara es de 192 px (SCROLL2 de 192 a 384), o sea 576 px de arte, y una vista de
512 px en un extremo asoma 64 px fuera de él. Todo en `bitacora/2026-10-20-mod-widescreen-viabilidad.md`.

## 4. Pruebas

`make test` incluye `test_widescreen` (centro idéntico en 42.000 cuadros de Ryu y de Zangief, HUD, estados guardados entre anchos), `test_wide_logic` y `test_stage_full`; `make compare-video` da 17/17
con el mod apagado. Rendimiento: +33 % de píxeles; el vídeo cuesta unos 0,2 ms por cuadro en macOS.

## 5. Límites conocidos

- El reflejo de los extremos es una invención plausible, no arte de Capcom; los fondos con rótulos o figuras junto al borde (Dhalsim, Ken) muestran su copia reflejada.
- Junto al retrato de la selección la barra de color termina en el borde del retrato; el panel del final queda con barras negras (es una imagen enmarcada).
- Lógica ancha: no se han revisado a fondo los ataques que empujan al rival contra la pared, los proyectiles que se descartan al salir de pantalla, los jefes de bonus ni la IA; la dificultad no está
  reajustada (los combos de esquina duran menos).
- El HUD depende de la asignación de ranuras del juego; si una pantalla usara otras ranuras, esa parte quedaría en el centro.
- Con la lógica original, los proyectiles siguen desapareciendo donde los borra el juego (su ventana de vida es lógica y no se toca), y los objetos que el juego crea al
  entrar en su ventana de actividad (`0x280E`) nacen donde la recreativa los crea.
- La máquina sombra no ve los retoques del modo entrenamiento en el VBlank del cuadro que repite; con el entrenamiento activo puede haber un cuadro de diferencia en los
  sprites de las bandas.

## 6. Cómo se hizo, por fases

| Fase | Qué | Decisión | Bitácora |
|---|---|---|---|
| 0-1 | Ancho de vídeo como parámetro, escenas ensanchables | D53 | `mod-widescreen-fases-0-1` |
| 2 | Relleno de los extremos por reflejo | D54 | `mod-widescreen-fase-2` |
| 3 | HUD en los bordes | D55, D56 | `mod-widescreen-fase-3` |
| — | Pantallas fijas con barras del color de fondo, bonus ensanchados | D57 | `mod-widescreen-pantallas-fijas` |
| 4 | Lógica ancha (parches en el recompilador) | D58 | `mod-widescreen-logica-ancha` |
| 5 | Stage completo y ultraancho | D59 | `ultrawide-*` |
| — | Sprites de las bandas con máquina sombra | D62 | `2026-11-04-pantalla-ancha-sprites` |
| — | Borde izquierdo: margen del constructor de sprites | D69 | `2026-11-12-pantalla-ancha-borde-izquierdo` |
