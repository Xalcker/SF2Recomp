# 2026-10-20 · Mod widescreen · Fases 0 y 1: ancho de vídeo parametrizado y escenas ensanchables

## Pregunta de partida

¿Se puede entregar un cuadro de hasta 512 píxeles de ancho sin tocar nada con el modo apagado, y ensanchar solo las escenas
que lo admiten?

## Qué se hizo

- `runtime/video.[ch]`: `Video.w` (ancho entregado), `rw`/`rx0` (geometría del cuadro renderizado), buffers al máximo de 512;
  `video_set_width`; `video_scene_wide`; sprites que cruzan la X 512 reaparecen por la izquierda (solo si `rw > 384`);
  escenas de 384 centradas con barras negras cuando el cuadro es más ancho.
- `runtime/sf2.[ch]`: `sf2_set_video_width`, `sf2_frame_width`, `SF2_SCREEN_W_MAX`.
- `platform/config.[ch]`: `widescreen`, `wide_width` (par, 384-512, 512 por omisión) y tecla `widescreen` (F9).
  `platform/hostio.c`: PNG con el ancho real.
- `app/main.c`: textura, captura, PNG de `--png-frames` y rectángulo de destino con el ancho del cuadro (la altura no cambia,
  así `aspect = 4:3` con 512 da 16:9); `--widescreen`; F9 alterna en caliente (rehace la textura y ajusta el ancho de la ventana).
- Criterio de escena (barrido de `ryu.txt` cada 5 cuadros): se ensancha con modo 2, fase 4 (`0xFF8001/05`), subfase 0x08, 0x0A, 0x0C o
  0x10 (presentación, pelea, victoria, recuento) y `0xFF86C6` ≠ 0 (el oponente existe; las fases de bonus no lo tienen). Las demás
  subfases son el VS (0x04), el mapa (0x02) y el diálogo del vencido (0x12), todas compuestas para 384.
- `tests/rom_tests.c`: `test_widescreen` (dos máquinas con la misma secuencia, una a 384 y otra a 512, 3.000 cuadros).

## Resultados

| Comprobación | Resultado |
|---|---|
| Centro de 384 columnas del cuadro ancho frente al de 384 | idéntico en los 3.000 cuadros (1.307 anchos, 1.693 centrados) |
| Estado de la partida con ambos anchos | mismo CRC cada 500 cuadros |
| `make test` | 8 de 8 |
| `make compare-video` (modo apagado) | 17 de 17 idénticos a MAME |
| Capturas con `build/sf2 --widescreen --png-frames` | combate ancho; VS y bonus centrados con barras negras |
| Rendimiento (`--bench 6000 --bench-mode video`, `ryu.txt`) | 3.470 cps a 384, 2.900 a 512 (-16 %) |

## Pendiente

Fase 2: en los extremos de cámara de Honda, Balrog, Vega, Ken y Blanka el borde del arte sigue visible. Fase 3 (HUD) y 4 (lógica) opcionales.
No se probó la versión Web (el lienzo conserva `aspect-ratio: 384/224`; con 512 queda letterbox) ni se corrió `make compare-coverage` (el modo
apagado no cambia la ruta de vídeo de 384, pero no se repitió la corrida larga).
