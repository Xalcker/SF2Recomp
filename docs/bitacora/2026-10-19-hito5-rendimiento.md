# 2026-10-19 · Hito 5 · Rendimiento: cuadros por segundo del núcleo, con y sin vídeo y sonido, en macOS y en la web

## Pregunta de partida

Cuánto margen hay sobre el tiempo real (59,637 cuadros por segundo) en cada plataforma, cuánto cuesta cada parte (CPU recompilada,
vídeo, sonido y ventana) y si hace falta optimizar algo antes de seguir.

## Método

`sf2 --bench N --bench-mode core|video|full` (también `?bench=N&mode=…` en la página web), con la partida de validación
`tools/inputs_sf2.txt` (arranque, aviso, atracción, moneda, selección, combates, KO, continuar, fin de partida y demo; 12.000
cuadros del juego, 12.106 VBlank, 179 millones de instrucciones del 68000, 68 millones del Z80) y sin límite de velocidad:

| Modo | Qué corre |
|---|---|
| `core` | CPU recompilada + máquina CPS1; **sin vídeo ni sonido** (`Sf2Options.video = sound = 0`), sin ventana |
| `video` | lo anterior + el renderizador CPS-A/CPS-B-11 en cada VBlank; sin sonido, sin ventana |
| `full` | lo anterior + la placa de sonido (Z80, YM2151, OKI, mezcla a 48 kHz) + ventana de SDL (textura, escalado, presentación sin vsync) + dispositivo de audio abierto (las muestras no se encolan) |

Se mide el tiempo entre el primer y el último cuadro (reloj monotónico de SDL) y se divide el número de VBlank emulados (lo que
devuelve `sf2_run_frame`) entre él; «× tiempo real» es esa cifra entre 59,637. `Release` = `-O2 -g` en nativo, `-O2` en wasm. En
`full` el bucle emula durante 12 ms, presenta y vuelve a empezar, como el bucle del juego en modo rápido; en la web, el bucle
es `requestAnimationFrame`, así que `full` (12 ms de emulación por cada refresco de 16,7 ms) es una cota inferior de lo que haría
con presentación libre. Todo en el mismo equipo (Mac con Apple silicon, 8 núcleos, macOS 26, Xcode clang 21, Docker Desktop
parado salvo en las mediciones de Linux).

## Resultados

12.000 cuadros cada vez. macOS: tres ejecuciones por modo seguidas, con el sistema libre.

| Plataforma | `core` | `video` | `full` |
|---|---:|---:|---:|
| **macOS arm64, nativo** | 10.194 · 9.142 · 9.911 → **9.911** cuadros/s (166×) | 1.386 · 1.379 · 1.383 → **1.383** (23,2×) | 909 · 913 · 910 → **910** (15,3×) |
| **Web** (Chromium, Emscripten 6.0.13, wasm -O2) | 6.723 · 6.734 → **6.730** (112,8×) | 1.049 · 1.066 → **1.058** (17,7×) | 511 (8,6×, una medida) |
| Linux arm64 (Docker, nativo en la VM) | 10.503 · 9.288 (176× · 156×) | 1.438 · 1.410 | 939 · 872 (6.000 cuadros) |
| Linux x86-64 (Docker, emulado) | 5.878 · 5.643 | 911 · 990 | 614 · 636 (6.000 cuadros) |
| Windows x86-64 (Wine sobre x86-64 emulado, 3.000 cuadros) | 5.207 · 5.218 | – | – |

Lectura (macOS, mediana): el núcleo solo cuesta 0,10 ms por cuadro (unos 15.000 instrucciones del 68000 por cuadro, ~150 millones
por segundo); el vídeo añade 0,62 ms por cuadro (con mucho lo más caro: se limpian tres bitmaps de 384×224, se reconstruye la
paleta de 3.072 entradas y se dibujan tres capas y los sprites en cada VBlank, sin caché de tilemaps); el sonido y la ventana
añaden otros 0,38 ms (el Z80 son 68 millones de instrucciones en la partida y el YM2151 genera 55.930 muestras por segundo). El
presupuesto de un cuadro es 16,77 ms: el juego completo usa un 6,5 % de un núcleo del Mac, y en el navegador un 12 %.

Comparación nativo-web: el núcleo en wasm va al 68 % de la velocidad nativa (6.730 frente a 9.911) y el vídeo al 77 % (1.058 frente
a 1.383, más cerca porque el renderizador son bucles simples de enteros). Linux arm64 en la VM de Docker da lo mismo que macOS (el
núcleo es el mismo código con el mismo compilador); x86-64 y Wine van bajo emulación de x86 sobre Apple silicon y las cifras solo
dicen que el binario no se arrastra, no cuánto correría en un PC.

## Observaciones

- En el navegador integrado el refresco de `requestAnimationFrame` bajó a 4-6 por segundo cuando el sistema estaba sin memoria
  (Docker Desktop con su máquina virtual, compilaciones y ASan a la vez); el juego bajó con él. Las cifras de arriba son de
  momentos con el sistema libre; no hay medida fiable con carga.
- La web `full` es la medida menos firme (una sola ejecución y limitada por el bucle de animación).
- **No se optimiza nada más en este hito**: el margen frente al tiempo real es de 8,6× en el peor caso medido (la web con todo) y
  de 15× en nativo. Donde habría que mirar si hiciera falta (hito 6): el renderizador (caché de la paleta convertida a RGB, no
  limpiar `pix`/`pri` donde las capas los cubren, tilemaps por filas), el gancho `rt_hook` por instrucción (hoy una llamada fuera
  de línea con comprobaciones que el juego no usa; el 68000 corre a 150 M instrucciones/s con ella) y el Z80.

## Comandos

```bash
build/sf2 sf2.zip --quiet --inputs tools/inputs_sf2.txt --bench 12000 --bench-mode core    # también video y full
python3 -m http.server -d build-web 8765    # y: http://127.0.0.1:8765/index.html?bench=12000&mode=core  (elige sf2.zip; el resultado sale en la consola)
tools/linux-docker.sh                       # imprime las tres medidas en Linux al final
```
