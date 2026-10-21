# 2026-10-19 · Hito 3 · Máquina compartida, visor SDL3 y modo PNG sin ventana

## Pregunta de partida

Cómo ver el juego en una ventana sin duplicar el planificador de la IRQ2 ni la red de seguridad de
Musashi del hito 2, y cómo producir cuadros en PNG sin ventana para compararlos con MAME.

## Qué se hizo

### `harness/machine.c`: la máquina a ciegas como pieza compartida

El contenido de `harness/blind.c` (planificador por ciclos estimados, entrega de la IRQ2 en el límite de
instrucción, red de seguridad con Musashi, informe final) pasa a `harness/machine.c` con una única
instancia `g_machine` (los ganchos `rt_hook`, `rt_fatal`, `rt_stopped` y `rt_unknown` del código
generado son funciones globales) y tres ganchos:

- `on_vblank(mc)`: al cruzar el umbral de ciclos del cuadro, antes de marcar la IRQ2 pendiente. Aquí el
  vídeo renderiza el cuadro y latchea la tabla de objetos (orden de `vblank_begin` de MAME).
- `on_sample(mc, cuadro)`: en el punto de muestra 0xAD0 del manejador, tras escribir la línea del
  resumen. Aquí se escriben los PNG pedidos: es el instante en que `tools/mame_snap.lua` guarda su
  captura, así que el cuadro N es el mismo en los dos lados.
- `on_finish(mc, motivo)`: antes de salir.

`blind.c` queda como la línea de órdenes de `sf2_blind`; el resumen por cuadro, la tabla de E/S y la
partida a ciegas de 12.000 cuadros dan exactamente el mismo `build/summary_recomp.txt` que antes de la
refactorización (comprobado byte a byte), y `make check` sigue en OK.

### Modo PNG sin ventana (`sf2_blind --png-frames`)

`--png-frames a,b,c [--png-dir dir] [--png-prefix nombre] [--gfx dir]` carga las ROMs gráficas, renderiza
cada VBlank y escribe `dir/nombre_N.png` (384 x 224, RGB de 8 bits) en el punto de muestra de cada cuadro
de la lista, con una línea en stderr con el orden de capas, las habilitadas, los sprites dibujados, los
registros CPS-A/CPS-B y los contadores de prioridad. El escritor PNG es propio (`png_write_rgb` en
`runtime/video.c`): bloques zlib sin comprimir, CRC-32 y Adler-32, sin dependencias.

### Visor `sf2_video` (`harness/view.c`, SDL3 de Homebrew)

```
sf2_video rom.bin [--inputs archivo] [--frames N] [--gfx dir] [--scale N] [--nothrottle] [--keyboard]
          [--png-frames a,b,c --png-dir dir] [--frame-cycles N] [--quiet]
```

Ventana de 384·N x 224·N (N = 3 por defecto), textura `XRGB8888` de 384 x 224 escalada sin filtrado. En
cada VBlank: cuadro → textura → `SDL_RenderPresent`, eventos y, salvo `--nothrottle`, espera hasta el
siguiente múltiplo de 16,768 ms (167.680 ciclos a 10 MHz: 59,637 Hz) con `SDL_DelayNS`; si el visor se
retrasa más de diez cuadros no intenta recuperarlos. La partida la dirige la tabla de entradas; con
`--keyboard` se suman las teclas del anfitrión (flechas, Z/X/C puñetazos, A/S/D patadas, 5 moneda, 1
start, F2 servicio: `Cps1.manual_in0/1/2`, que `cps1_apply_inputs` combina con la tabla). Teclas de
depuración: 1-4 alternan sprites y SCROLL1-3, F12 guarda `sf2_video_N.png`, Escape termina. El título de
la ventana muestra el cuadro, el orden de capas y los sprites dibujados. `make run-video` lo lanza con la
partida de `tools/inputs_sf2.txt`.

### Makefile

| Objetivo | Qué hace |
|---|---|
| `make run-video` | visor con la partida de INPUTS durante BFRAMES cuadros |
| `make png-video` | PNG del runtime de los cuadros VFRAMES en `build/video/` |
| `make mame-video` | capturas y volcados de MAME de los mismos cuadros en `build/video/mame/` (ver la entrada de comparación) |
| `make compare-video` | los dos anteriores más `tools/compare_frames.py`; diffs en `build/video/diff/` |

`VFRAMES` (lista por defecto de 17 cuadros), `GFXDIR` (ROMs gráficas, por defecto `ROMDIR`) y `VDIR`
(directorio de salida) se pueden cambiar en la línea de órdenes; `SDL_CFLAGS`/`SDL_LIBS` salen de
`pkg-config sdl3` con los valores de Homebrew como reserva.

## Comandos

```bash
make -j8                     # build/sf2_blind, build/sf2_video, build/render_dump (+ sf2_lockstep)
make run-video               # ventana
build/sf2_video build/rom.bin --inputs tools/inputs_sf2.txt --frames 1650 --nothrottle --png-frames 1600 --png-dir build/video/view
build/sf2_blind build/rom.bin --frames 10001 --inputs tools/inputs_sf2.txt --png-frames 100,650,1600 --png-dir build/video --quiet
```

## Resultados

- `sf2_blind` con vídeo: 10.001 cuadros (10.107 VBlank renderizados) en 7,5 s, frente a ~6 s sin vídeo;
  el renderizado cuesta unos 0,15 ms por cuadro en este equipo (clang -O2 para `video.c`/`gfx.c`).
- El visor a velocidad libre hace los mismos 1.650 cuadros en ~2 s con la ventana abierta y su PNG del
  cuadro 1600 es idéntico al de MAME; a velocidad real avanza a 59,6 Hz.
- El resumen de 12.000 cuadros y `make check` no cambian con la refactorización.

## Pendiente

- Sonido (hito 4) y una ventana con relación de aspecto configurable o pantalla completa (hito 5).
- El visor ejecuta el juego en el hilo de la ventana; si el anfitrión bloquea los eventos (por ejemplo al
  arrastrar la ventana en macOS) el juego se detiene y luego salta.
