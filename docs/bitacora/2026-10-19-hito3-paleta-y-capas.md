# 2026-10-19 · Hito 3 · Paleta y capas de scroll

## Pregunta de partida

Cómo pasar de la RAM gráfica (0x900000-0x92FFFF) y de los registros CPS-A / CPS-B-11 a un cuadro de
384 x 224 con las tres capas de scroll en el mismo orden, posición y color que MAME.

## Qué se hizo

### Paleta: una RAM propia del CPS-B que se copia al escribir la base de paleta

En la placa la CPU escribe la paleta en la RAM gráfica y el CPS-B la copia a su RAM de paleta. MAME
(`cps1_build_palette`) hace la copia **en cada escritura del registro de base de paleta** (0x80010A),
con el control de paleta (0x800170) vigente en ese instante: por cada una de las seis páginas de 512
entradas (OBJ, SCROLL1, SCROLL2, SCROLL3, estrellas 1 y 2), si su bit está a 1 se copia la página
siguiente de la RAM gráfica; si está a 0 se salta una página de la RAM gráfica solo si ya se copió
alguna antes. SF2 escribe 0x3F (las seis páginas) y la base 0x9000 (RAM gráfica + 0) en cada manejador
de IRQ2 (0xB2A-0xB38), después del punto de muestra.

Se implementa en `runtime/mem.c` (`mem_palette_copy`, `m->palette[0xC00]`), como estado del bus: así
la paleta que ve el cuadro N es la copiada en el manejador N−1, aunque las tareas hayan cambiado la
RAM gráfica entre medias (fundidos), y el arnés en paso cerrado compara también esa RAM
(`mem_compare`). La base se alinea a 0x400 bytes y se acota a 0x3FFFF como `cps1_base`.

Conversión a RGB (`build_rgb` en `runtime/video.c`), idéntica a MAME: entrada `bbbbRRRRGGGGBBBB`,
`brillo = 0x0F + 2·b`, `R = r·0x11·brillo / 0x2D` (división entera), y lo mismo para G y B. Con brillo
15 (0x2D) el canal vale r·0x11 (0x00-0xFF); con brillo 0 queda a un tercio.

### Coordenadas de la pantalla

La pantalla CPS1 son 512 x 262 ciclos de píxel; la zona visible va de (64, 16) a (447, 239)
(`CPS_HBEND`, `CPS_VBEND` en `cps1.h`). Sprites y scrolls se expresan en esas coordenadas "crudas": el
píxel (0, 0) del PNG es el (64, 16) de la placa (`VIDEO_RAW_X0/Y0`). El fondo es el pen 0xBFF (último
de la paleta), como `bitmap.fill(0xbff)`.

### Capas de scroll

Tres tilemaps de 64 x 64 tiles (`tilemapN_scan` de MAME da el índice de tile para (columna, fila)):

| Capa | Tile | Tamaño | Base (CPS-A) | Scroll X/Y (CPS-A) | Índice de tile | Página de paleta |
|---|---|---|---|---|---|---|
| SCROLL1 | 8x8 | 512 x 512 | 0x800102 | 0x80010C / 0x80010E | `(fila & 0x1F) + (col << 5) + ((fila & 0x20) << 6)` | 1 (0x200 + color·16) |
| SCROLL2 | 16x16 | 1024 x 1024 | 0x800104 | 0x800110 / 0x800112 | `(fila & 0x0F) + (col << 4) + ((fila & 0x30) << 6)` | 2 (0x400 + …) |
| SCROLL3 | 32x32 | 2048 x 2048 | 0x800106 | 0x800114 / 0x800116 | `(fila & 0x07) + (col << 3) + ((fila & 0x38) << 6)` | 3 (0x600 + …) |

Cada tile ocupa dos palabras en la RAM de la capa (base = registro × 256 alineado a 0x4000): código y
atributo. Atributo: bits 0-4 color, bit 5 volteo X, bit 6 volteo Y, bits 7-8 grupo de prioridad (ver la
entrada de sprites); el código de SCROLL3 se enmascara con 0x3FFF. El píxel visible (X, Y) en
coordenadas crudas muestra el píxel `((X + scrollX) mod ancho, (Y + scrollY) mod alto)` del tilemap.
Habilitación: bits 0x08 / 0x10 / 0x20 del control de capas (0x800166, `CPS_B_11`) y, para SCROLL2 y
SCROLL3, también los bits 2 y 3 del control de vídeo (0x800122). Orden de dibujo: bits 6-7, 8-9,
10-11 y 12-13 del control de capas (0 = sprites). Pen 15 transparente.

Row scroll (bit 0 del control de vídeo): SCROLL2 toma para la fila cruda Y el desplazamiento
`scrollX2 + OTHER[(Y + offset) & 0x3FF]`, donde OTHER es la zona de 0x800 bytes cuya base está en
0x800108 y el offset en 0x800120. Es exactamente lo que resulta de los 256 `set_scrollx` de
`screen_update_cps1` (se indexa por fila de pantalla, no por fila del tilemap); SF2 lo tiene activo en
el combate (0x800122 = 0x3F) y la base OTHER alterna entre dos zonas (0x9200/0x9220/0x9230…) cada
cuadro, como la de los objetos.

Lo que SF2 hace con las capas en el combate (cuadro 1600, control de capas 0x0E78): orden 1, 2, 3, 0,
es decir SCROLL1 al fondo, luego SCROLL2, SCROLL3 y los sprites encima. SCROLL1 (8x8) dibuja el cielo
con nubes, SCROLL2 el suelo y SCROLL3 los árboles y la cabaña; el marcador, los nombres, "ROUND 1" y
los luchadores son sprites.

### Dibujo

`render_tilemap` recorre las 224 filas visibles y, dentro de cada fila, avanza tile a tile (hasta 8,
16 o 32 píxeles por consulta a la RAM) usando los pens predecodificados; el volteo se aplica al índice
de fila y de columna dentro del tile. No hay caché de tilemaps como en MAME: un cuadro completo con las
tres capas y los sprites cuesta ~0,5 ms, así que no hace falta.

## Comandos

```bash
make -j8
build/sf2_blind build/rom.bin --frames 1601 --inputs tools/inputs_sf2.txt --png-frames 1600 --png-dir build/video
SF2_VIDEO_LAYERS=8 build/sf2_blind ... --png-prefix capa8      # solo SCROLL3 (bit 0 sprites, 1-3 capas)
SF2_VIDEO_PROBE=95,185 build/sf2_blind ...                       # pen y prioridad de un píxel tras cada capa
python3 -I tools/compare_frames.py build/video/cuadro_1600.png build/video/mame/cuadro_1600.png --diff build/video/diff_1600.png
```

## Resultados

- Primer cuadro renderizado (1600, combate): 66 % de píxeles distintos porque faltaban SCROLL2 y SCROLL3.
  Causa: las bases de las capas son registros consecutivos (0x800102/104/106) y el código las leía con
  paso 2 como los scrolls; SCROLL2 leía la base de SCROLL3 y SCROLL3 la de la paleta. Con la corrección,
  1,3 % de píxeles distintos, todos en los sprites (ver la entrada de sprites).
- Las variables `SF2_VIDEO_LAYERS` (máscara de capas) y `SF2_VIDEO_PROBE` (sonda de un píxel) se dejan
  como ayudas de depuración documentadas en `runtime/README.md`.
- Tras la entrada de sprites: los 17 cuadros de referencia coinciden píxel a píxel con MAME, incluido
  el row scroll del combate y el fundido (paleta) de los cuadros 6200-8000.

## Pendiente

- Volteo de pantalla (bit 15 del control de vídeo) y capa de estrellas: no implementados; SF2 no los usa
  (D24). El renderizador avisa una vez si el juego activara el volteo.
