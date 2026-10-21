# 2026-10-19 · Hito 3 · Comparación píxel a píxel con MAME y cierre del hito

## Pregunta de partida

Cómo obtener de MAME capturas de los mismos cuadros que produce el runtime, con la misma partida y la
misma numeración, y cómo separar un error del renderizador de una diferencia de estado o temporización.

## Qué se hizo

### `tools/mame_snap.lua`

Hermano de `mame_blind.lua`: la misma tabla de entradas (`SF2_INPUTS`), el mismo tap de lectura sobre
0x800172 filtrado por `CURPC == 0xAD0` para contar cuadros, y en los cuadros de `SF2_SNAP_FRAMES`
guarda `cuadro_N.png` con `screen:snapshot()`. Con `SF2_DUMP_DIR` vuelca además, por cuadro:

| Archivo | Contenido | Cuándo se lee |
|---|---|---|
| `gfx_N.bin` | RAM gráfica (0x30000 bytes) | en el tap (punto de muestra) |
| `regs_N.bin` | CPS-A (0x40 bytes) y CPS-B (0x40 bytes), big-endian, vía `memory.shares[":cps_a_regs"]` | en `emu.register_frame_done` |
| `obj_N.bin` | tabla de objetos (0x800 bytes) **usada** para dibujar el cuadro | la leída en el `frame_done` anterior |
| `pal_N.bin` | los 0xC00 pens de MAME como 0x00RRGGBB (`screen.palette:pen_color`) | en el tap |

Por qué así: `screen:snapshot()` devuelve el último cuadro compuesto, que es el que `vblank_begin`
renderizó justo antes de la IRQ2 (`video_manager::frame_update` precede a las llamadas de VBlank y a la
IRQ; la captura no vuelve a renderizar). `register_frame_done` se ejecuta dentro de `frame_update`,
después de renderizar y antes de `cps1_objram_latch` y de la IRQ, así que ahí los registros son los
del cuadro recién dibujado (el manejador escribe la base OBJ y OTHER en 0xAC0/0xAC8, antes del punto de
muestra) y la tabla de objetos en la base OBJ es la que se va a copiar para el siguiente cuadro; la
usada por este es la del `frame_done` anterior. La paleta de MAME solo cambia al escribir 0x80010A
(después del tap), por lo que leerla en el tap vale.

`-snapshot_directory` en la línea de órdenes prevalece sobre el de `tools/mame.sh`, así que las
capturas van a `build/video/mame/`; sin `-video none` ni los proveedores de entrada a `none` (D20) la
partida no sería reproducible. Comprobado: la primera lectura de `cps_a_regs` dio `9100 90c0 9040 9080
9200 9000 …`, la secuencia de bases que escribe el juego.

### `tools/compare_frames.py`

Lee y escribe PNG con `zlib` de la biblioteca estándar (8 bits, RGB o RGBA, filtros 0-4; es lo que
escriben MAME y `runtime/video.c`), sin Pillow ni numpy (D4). Dos archivos o dos directorios con una
lista de cuadros; por cuadro imprime píxeles distintos, el rectángulo que los contiene y los primeros
con el color de cada lado, y escribe `diff_N.png` (la captura atenuada con los píxeles distintos en
rojo, o en amarillo si solo cambia el brillo). Código 0 si todo coincide, 1 si hay diferencias, 2 si
falta un archivo. `--max-bad N` tolera N píxeles por cuadro (0 por defecto: píxel a píxel).

### `tools/render_dump.c` → `build/render_dump`

Renderiza un cuadro a partir de los cuatro volcados de MAME (`gfx`, `regs`, `obj`, `pal`) con
`runtime/video.c`, sin ejecutar el juego. Si su salida coincide con `cuadro_N.png` el renderizador es
correcto y cualquier diferencia del runtime completo viene del estado o de la temporización; si no,
el fallo está en el dibujo. Fue la herramienta que localizó el error de orden de los sprites: la
imagen desde los volcados daba exactamente los mismos 1.129 píxeles distintos que el runtime.
`--pal-runtime` reconstruye la paleta con `mem_palette_copy` en vez de usar los pens de MAME
(comprueba también la copia y la fórmula de brillo).

### Flujo

```bash
make compare-video                 # png-video (runtime) + mame-video (MAME, ~3 min para 10.000 cuadros) + comparación
make compare-video VFRAMES=100,650,1600 VDIR=build/video_otro
for n in 100 650 1600; do build/render_dump build/video/mame/gfx_$n.bin build/video/mame/regs_$n.bin \
    build/video/mame/obj_$n.bin build/video/mame/pal_$n.bin build/video/dump/cuadro_$n.png; done
python3 -I tools/compare_frames.py --dir-a build/video/dump --dir-b build/video/mame --frames 100,650,1600
```

## Resultados

Lista de referencia (`VFRAMES`): 100 (test de RAM), 650 (aviso), 900 (título), 1100 (selección de
personaje), 1250 (Ryu confirmado), 1600, 1800, 2200, 3000, 3702, 4400, 5000 (combate contra Blanka,
con row scroll y los luchadores superpuestos a sombras y decorado), 5600 (KO), 6200 (continuar), 7000
(fin de partida), 8000 (título sin crédito), 10000 (demo Blanka contra Guile).

| Comparación | Cuadros | Resultado |
|---|---:|---|
| Runtime completo (`make png-video`) frente a MAME | 17 | 17 idénticos |
| Renderizador sobre los volcados de MAME (`render_dump`) frente a MAME | 17 | 17 idénticos |
| Runtime completo, muestreo denso: un cuadro cada 100 (100-12000) más los 18 cuadros que diferían en RAM en el hito 2 (787, 1262-1263, 1441, 1561-1563, 1582, 3702, 3796-3798, 6091, 7402, 9575, 9590, 11400, 11635) | 137 | 137 idénticos |

Historial hasta llegar ahí (cuadro 1600): 66 % de píxeles distintos (bases de SCROLL2/3 mal leídas) →
1,31 % (orden de los sprites: el bit 31 implícito de `prio_transpen`) → 0. Los 18 cuadros del hito 2 en
que la IRQ cortó una tarea larga en otra instrucción dan la misma imagen que MAME: la diferencia de RAM
está en zonas que el cuadro de ese VBlank no muestra o que convergen antes del siguiente.

Tiempos: MAME 12.001 cuadros con 137 capturas y volcados en 202 s; el runtime los 12.000 cuadros con
vídeo en ~9 s; la comparación de 137 PNG en Python ~25 s.

## Estado del hito 3

Cumplido el criterio del plan: las capturas del arranque, la selección de personaje y el combate
coinciden píxel a píxel con MAME, sin diferencias que explicar. Entregables: `runtime/gfx.c`
(ROMs gráficas, formato de tiles, mapeador STF29), `runtime/video.c` (paleta, capas, sprites,
prioridades, PNG), la copia de paleta en `runtime/mem.c`, `harness/machine.c` (máquina compartida),
`harness/view.c` (`sf2_video`, SDL3), `sf2_blind --png-frames`, `tools/mame_snap.lua`,
`tools/compare_frames.py`, `tools/render_dump.c`, objetivos `run-video`, `png-video`, `mame-video` y
`compare-video`, decisiones D21-D26, READMEs y estas cinco entradas.

## Pendiente

- Capa de estrellas y volteo de pantalla (no usados por SF2, D24).
- Las máscaras de prioridad del CPS-B-11 están implementadas según MAME pero SF2 no las usa en la
  partida de referencia (valen 0 en todos los cuadros capturados).
- Hito 4: sonido.
