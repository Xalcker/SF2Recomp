# 2026-10-19 · Hito 6 · Rendimiento: perfil del núcleo y optimizaciones que no cambian ni un bit

## Pregunta de partida

Dónde se va el tiempo en macOS (CPU recompilada, vídeo, sonido) y qué se puede acelerar sin que cambie ningún resultado de las
comparaciones: resúmenes por cuadro, comandos de sonido, capturas, WAV y CRC de los estados.

## Método

- `sf2 --bench 12000 --bench-mode core|video|full` con la partida de validación (`tools/inputs_sf2.txt`), como en el hito 5
  (modos explicados en `2026-10-19-hito5-rendimiento.md`), `SDL_VIDEODRIVER=dummy`, tres ejecuciones por modo y la mediana. El
  binario «antes» es una copia del juego compilado con el reloj exacto (D47) justo antes de optimizar; los dos se midieron
  seguidos, con el sistema libre (un proceso de Python olvidado que ocupaba un núcleo se paró antes de medir).
- Perfil con `sample` (muestreo de pila de macOS) sobre ejecuciones largas de cada modo, agrupando las funciones por parte.

## Lo que decía el perfil

| Parte (modo `full`, hilo de emulación) | Antes | Después |
|---|---:|---:|
| Vídeo (`render_tilemap`, `gfx_map`, `video_render`) | 57 % | 40 % |
| Sonido (YM2151 de ymfm, Z80, OKI, mezcla) | 27 % | 40 % |
| CPU 68000 recompilada y máquina | 12 % | 15 % |
| Ventana de SDL y eventos | 4 % | 5 % |

Y dentro de la CPU (modo `core`): `rt_hook` y `poll` eran el **61 %** del tiempo; el código generado, el 12 %. El gancho se llama
antes de cada instrucción (unas 15.000 por cuadro, la mayoría en el bucle de espera del juego en 0xA50) y era una llamada con un
`switch` por instrucción sobre los tipos de K y E de D47 (que cambian de una instrucción a otra: saltos mal predichos) y otra
llamada a `poll`.

## Qué se cambió

Vídeo (`runtime/video.c`, `runtime/gfx.c`):

1. `gfx_init` precalcula el mapeador STF29 para los 65.536 códigos de cada tipo de tile (antes: un recorrido de la tabla de
   rangos con desplazamientos por cada tile dibujado) y la conversión de las 65.536 palabras de paleta a RGB (antes: tres
   divisiones por entrada y cuadro para 3.072 entradas). 1,25 MB más de memoria, una vez.
2. Los tilemaps guardan lo que necesitan de cada tile (fila de pens, color, máscara de prioridad, volteos) para la fila de tiles
   en curso: se lee la RAM gráfica y se mapea una vez por fila de tiles y columna, no en cada fila de píxeles (8, 16 o 32 veces
   menos).
3. El bucle de píxeles de un tramo de tile ya no tiene saltos por píxel (selección del pen 15 sin `if`, bucles separados para
   el tile volteado) y se instancia por capa con el tamaño de tile constante, de modo que un tramo entero de 8, 16 o 32 píxeles
   se vectoriza; se quitó un `memcpy` de longitud variable que acababa en `__memcpy_chk`.
4. Los sprites se recortan a la pantalla por filas y columnas antes del bucle, en vez de comprobar cada píxel.

CPU (`runtime/machine.c`):

5. `rt_hook` tiene un camino rápido: el punto de muestreo de D47 se calcula con selecciones en vez de `switch` (el compilador
   emite `csel`), `poll` solo se llama si llegó el VBlank o hay una IRQ pendiente (es exactamente cuando hace algo), y lo raro
   (punto de muestra de entradas, límite de instrucciones, ganchos de depuración, cesión) está fuera de línea. Quedan unas 45
   instrucciones de máquina por instrucción del 68000.

No se tocó: el YM2151 (ymfm, de terceros), el Z80 (interpretado, hito 4) ni el bucle de espera del juego (saltarlo exigiría
calcular a qué instrucción llega la IRQ con el modelo de D47 sin ejecutarla; el riesgo no compensa con 230 veces el tiempo real).

## Resultado

Cuadros por segundo, mediana de tres (12.000 cuadros):

| Modo | Hito 5 (otra sesión) | Antes | Después | Mejora |
|---|---:|---:|---:|---:|
| `core` (CPU + máquina) | 9.911 (166×) | 11.537 (193×) | **13.761 (231×)** | +19 % |
| `video` (+ renderizador) | 1.383 (23×) | 1.809 (30×) | **3.669 (62×)** | ×2,03 |
| `full` (+ sonido + ventana) | 910 (15×) | 1.217 (20×) | **1.875 (31×)** | +54 % |

Por cuadro, después: 0,073 ms de CPU, 0,20 ms de vídeo (antes 0,47) y 0,26 ms de sonido y ventana; el juego completo usa un 3,2 %
de un núcleo (antes un 4,9 %; en el hito 5 se midió un 6,5 % en otras condiciones de carga, por eso la comparación válida es la de
la misma sesión).

**Ningún resultado cambia** (comprobado con el binario final):

- `make coverage` con el núcleo nuevo: resumen por cuadro (con PC, SR y registros), comandos de sonido, direcciones ejecutadas y
  registro de despachos idénticos byte a byte a los del binario anterior en las 14 secuencias (603.050 cuadros).
- `playgen.py --verify`: los 13 `#@ state_crc` coinciden.
- Capturas: el `sf2_blind` anterior y el nuevo renderizan uno de cada 37 cuadros de las 14 secuencias: **16.291 de 16.291 PNG
  idénticos** byte a byte.
- Las comprobaciones de los hitos anteriores (ver la entrada de cierre) y la validación contra MAME siguen dando lo mismo.

## Comandos

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sf2 sf2.zip --quiet --inputs tools/inputs_sf2.txt --bench 12000 --bench-mode core
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sf2 sf2.zip --quiet --inputs tools/inputs_sf2.txt --bench 12000 --bench-mode video
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sf2 sf2.zip --quiet --inputs tools/inputs_sf2.txt --bench 12000 --bench-mode full
sample <pid> 8 -file perfil.txt            # durante una ejecución larga (--bench 60000)
```

## Pendiente

- Donde queda tiempo: el sonido (ymfm y el Z80 interpretado, ahora tanto como el vídeo), la llamada por instrucción a `rt_hook`
  (sobre todo en el bucle de espera) y el vídeo (limpiar `pix`/`pri` y convertir 86.016 píxeles por cuadro). Nada de eso hace
  falta para jugar: el margen es de 31 veces el tiempo real.
- No se volvió a medir la web ni Linux/Windows (el código es el mismo C; ver la entrada de cierre para lo que se compiló).
