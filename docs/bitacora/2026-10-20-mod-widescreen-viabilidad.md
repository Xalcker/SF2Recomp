# 2026-10-20 · Mod widescreen · Viabilidad y plan

## Pregunta de partida

¿Qué hace falta para mostrar más escenario a los lados (sin estirar) y qué se ve hoy si solo se ensancha el renderizador?

## Qué se hizo

1. Se leyeron `runtime/video.c`, `runtime/video.h` y `runtime/gfx.c`: la anchura (`VIDEO_W` 384, origen crudo X 64) solo está
   en `video.[ch]`, `sf2.h` (`SF2_SCREEN_W`), `app/main.c`, `hostio.c`, `tests/` y `tools/sf2py.c`.
2. Prototipo desechable en el scratchpad (no está en el proyecto): copia de `sf2.c` y `video.[ch]` con `VIDEO_W = 512`
   y `VIDEO_RAW_X0 = 0`, enlazada con las bibliotecas de `build/`, más un programa `wide.c` que corre una secuencia y vuelca
   PNG, registros de scroll y, en modo `scan`, los píxeles sin pintar de las bandas 0-63 y 448-511 cada 10 cuadros.
3. Se corrió `tools/sequences/ryu.txt` (40.000 cuadros): 4.000 muestras, 2.321 con píxeles de fondo sin pintar a los lados.
4. Con el desensamblado (`build/disasm.txt`) se localizó la cámara: `0xFF8036/3A/3E` (X lógica de SCROLL1-3) y la copia a los
   registros con un cuadro de retraso en `0x1736` (`0xFF8060/64/68`); y el motor de transmisión de columnas
   (`0x083FE8` y siguientes).

## Resultados

- Rango de SCROLL2 X por escenario: 192-384 (recorrido de 192 px, arte de 576 px). Casos aparte: Ken (barco) 0-208, Dhalsim
  99-384.
- Píxeles sin pintar en la banda derecha (máximo en el escenario): Blanka 497, Honda 8.996, Ken 445, Balrog 4.305-4.468,
  Vega 1.501; en la izquierda: Ken 314, Balrog 71. Guile, Zangief, Chun-Li y Dhalsim, casi nada. Bonus: 14.336 en ambos lados.
- Selección de personaje y pantallas fijas: restos de tiles en las bandas.
- Sprites: sin fantasmas a los lados en ninguna captura de combate.

El plan completo, con fases y decisiones abiertas, está en `docs/WIDESCREEN.md`.

## Pendiente

Nada implementado. Falta confirmar las decisiones del apartado 6 del plan y empezar por las fases 0 y 1.
