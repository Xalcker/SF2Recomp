# 2026-10-20 · Hito 6 · Linux, Windows y la web con el código final: las mismas huellas que macOS

## Pregunta de partida

El hito 6 cambió el núcleo: reloj exacto del 68000 (D47), vídeo y `rt_hook` optimizados (D51) y un C generado nuevo. Al cerrarlo
solo se repitieron las comprobaciones en macOS. La web y Windows compilaban, pero no se volvieron a ejecutar las pruebas de Linux
(Docker), la prueba de humo de Windows (Wine) ni las pruebas wasm en Node. ¿Sigue siendo la misma partida en todas las
plataformas con el criterio de D45, es decir, el mismo CRC del estado y del vídeo en los 12 puntos de la partida de 12.000 cuadros
y, en x86-64, el audio dentro de D33?

## Referencia: macOS arm64 con el código final

`build/` al día (`cmake --build build` no tuvo nada que hacer; el arreglo de abajo no lo cambia):

```
cuadro=1000   estado=3a459583 video=6ca98184 audio=fbfbee8a
cuadro=2000   estado=364bcffb video=29101f30 audio=9bfefbe1
cuadro=3001   estado=18126186 video=05db23c3 audio=ed2a6227
cuadro=4001   estado=518e9a3b video=22abe1ac audio=b21c6d33
cuadro=5000   estado=7260a632 video=f94c77aa audio=89ad619a
cuadro=6001   estado=7885cb0c video=2d912223 audio=c8050e89
cuadro=7000   estado=d3f571dd video=0313deb5 audio=e49e83e0
cuadro=8000   estado=3ce8c88a video=32c1d8d3 audio=fa913ac3
cuadro=9000   estado=bc695caa video=9573608f audio=15221b8a
cuadro=10000  estado=02ae674f video=6d454326 audio=097d50bf
cuadro=11000  estado=4682648b video=70d9bf76 audio=f95d2976
cuadro=12000  estado=1e8b50c9 video=5f77cf89 audio=9074d9fc
```

El resumen por cuadro de `sf2_blind` (12.000 cuadros) tiene ahora el SHA-256 `63aa2b5a…`. El `29339a91…` de los hitos 2 a 5 se
obtuvo con el reloj de Musashi; con el de D47 cambian el PC interrumpido y los registros de muchos cuadros. El WAV sigue siendo
`72f624f2…`, el de MAME. Los estados y los cuadros de la traza también cambian respecto a los del hito 5 (cuadros 3001, 6001,
10000…) por el mismo motivo. Por eso las comparaciones se hacen contra esta referencia nueva, no contra
`build/hito5_evidencia/`.

## Qué se hizo

- **Linux**: `tools/linux-docker.sh` (arm64 nativo en la VM) y `PLATFORM=linux/amd64 tools/linux-docker.sh` (x86-64 emulado).
  Se reutilizaron los volúmenes de compilación del hito 5: CMake volvió a configurar, regeneró el C (43.036 instrucciones, 1.339
  funciones, 11.325 etiquetas, 160.448 líneas, **idéntico byte a byte al de macOS**) y recompiló lo que había cambiado. La imagen es la
  misma: Debian 13, clang 19.1.7 y SDL 3.2.10.
- **Windows**: `cmake --build build-win` no tuvo nada que hacer (los `.exe` son los del arreglo `BUILD_ALWAYS` de la entrada de
  cierre); después, `tools/windows-wine-docker.sh`.
- **Web**: `make web` compila sin pruebas (`SF2_BUILD_TESTS=OFF`), así que los `sf2_*_tests.wasm` de `build-web/` eran del hito 5.
  Se compilaron en un árbol temporal (`emcmake cmake -S . -B <tmp> -DSF2_ROM_DIR=$PWD/sf2.zip`, objetivos `sf2_unit_tests`,
  `sf2_rom_tests` y `sf2_trace`) y se pasaron `ctest` y `sf2_trace` en Node 24.19 (Emscripten 6.0.13).
- **Además**: `playgen.py --verify` de las 13 secuencias del hito 6 con `libsf2py.so` en Linux arm64 y x86-64.
- Salidas (logs, trazas, WAV, informes de `compare_audio.py`, medidas) en `build/hito6_plataformas/`.

## Fallo encontrado: `libsf2py.so` no enlazaba en Linux

La primera ejecución en arm64 se detuvo al compilar, antes de `ctest`:

```
FAILED: libsf2py.so
/usr/bin/ld: runtime/libsf2_core.a(ym2151.cpp.o): relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol `…' which may bind
externally can not be used when making a shared object; recompile with -fPIC
```

**Causa.** La biblioteca compartida que usa `tools/playgen.py` (objetivo `sf2py`, nuevo en el hito 6) enlaza las bibliotecas estáticas
del núcleo (`sf2_base`, `sf2_recomp`, `sf2_core`, `ymfm`, `sf2_hostio`). El proyecto fija `CMAKE_POSITION_INDEPENDENT_CODE OFF`, así
que CMake no añade ninguna opción, y el clang de Debian compila PIE por omisión. Un objeto PIE supone que sus símbolos no se pueden
sustituir desde fuera, cosa que una biblioteca compartida ELF no garantiza, y `ld` lo rechaza. En macOS no se veía porque Mach-O es
siempre PIC. Es lo que la entrada de secuencias dejaba anotado como pendiente.
Como `sf2py` está en el objetivo `all`, el fallo rompía `cmake --build` entero en Linux, no solo `playgen`.

**Arreglo** (`tools/CMakeLists.txt`, junto a `sf2py`): fuera de Apple y Windows, esas cinco bibliotecas se compilan con
`POSITION_INDEPENDENT_CODE ON` (`-fPIC`). No se tocó la opción global ni la web, que es una compilación cruzada y no entra en
`tools/`.

**Sin cambios en macOS (D51).** Se reconfiguró `build/`: `compile_commands.json` quedó idéntico byte a byte y no se recompiló ni un
objeto. **En Linux tampoco cambia ningún resultado.** Se hizo una comparación A/B en x86-64: un árbol sin el arreglo (solo `sf2` y
`sf2_trace`, que sí enlazan) contra el árbol con `-fPIC`. Las trazas de estado y vídeo salen iguales y la velocidad es la misma:
núcleo 7.483 · 7.699 · 7.741 cuadros/s con `-fPIC` frente a 7.375 · 7.577 · 7.692 sin él, en ejecuciones alternas.

## Resultados

| Plataforma | Pruebas | Estado y vídeo (12 puntos) | Resumen 12.000 cuadros | Audio |
|---|---|---|---|---|
| macOS arm64 (referencia) | `ctest` 8 de 8 (entrada de cierre) | – | `63aa2b5a…` | WAV `72f624f2…` |
| Linux arm64 (Debian 13, clang 19.1.7) | `ctest` **8 de 8** | **idénticos** | idéntico | **idéntico bit a bit**: WAV de `sf2_blind` `72f624f2…` y WAV de la traza iguales byte a byte a los de macOS |
| Linux x86-64 (ídem, emulado) | `ctest` **8 de 8** | **idénticos** | idéntico | 985 de 9.742.081 muestras difieren ≤ 1 LSB (SNR 111,7-112,6 dB); **D33 pasa** con 0,000 dB de diferencia |
| WebAssembly (Node 24.19, Emscripten 6.0.13) | `unit`, `roms_and_states`, `core_purity`: **3 de 3** | **idénticos** | – | las mismas 985 muestras: WAV igual byte a byte al de Linux x86-64; D33 pasa |
| Windows x86-64 (llvm-mingw, Wine) | `sf2_unit_tests` 633 y `sf2_rom_tests` **7.898** comprobaciones, 0 fallos | **idénticos** | – | WAV igual byte a byte al de Linux x86-64 y al de wasm; D33 pasa |

Las 8 pruebas de Linux son `unit`, `roms_and_states`, `lockstep_vs_musashi`, `summary_run_vs_step`, `core_purity`,
`game_frames_headless` (controladores `dummy` de SDL), `game_dispatch_failure` (nueva en el hito 6) e `input_mapping`.

En `compare_audio.py` (modo «solo WAV», tramos por el cuadro nominal) se analizan seis tramos. El de arranque dura un cuadro y no
llega a una ventana de análisis. Los seis pasan con energía, bandas y LSD a 0,000 dB. Por tramo, las muestras distintas son 0, 0,
20, 233, 141 y 591. Es la misma situación que en el hito 5 (D45): arm64 contrae la mezcla en float a FMA como el MAME de referencia y
x86-64/wasm no. Las tres plataformas sin FMA dan entre sí el mismo WAV byte a byte. El WAV de `sf2_blind` en x86-64 sigue siendo
`e057198f…`, como en el hito 5.

**Secuencias del hito 6 con `libsf2py.so`** (`playgen.py --verify`, en una copia de las secuencias porque `--verify` reescribe las
marcas): en Linux arm64 (54 s) y x86-64 (1 min 30 s, emulado), las 13 dan **el mismo CRC del estado final que el grabado en macOS**.
Son 591.050 cuadros, todas las secuencias salvo `validacion.txt`, que es la de la traza. La igualdad entre plataformas queda
comprobada así en el juego entero, no solo en la partida de validación. También queda probado que `libsf2py` y `playgen.py`
funcionan en Linux.

Wine imprime avisos propios al crear su prefijo (`XDG_RUNTIME_DIR is invalid…`,
`failed to open L"C:\\windows\\syswow64\\rundll32.exe": c0000135`), como el `rosetta error` del hito 5. No afectan a los
programas, que terminan con código 0 y dan las huellas de arriba.

## Rendimiento

Cuadros por segundo con `sf2 --bench` (partida de validación). Linux: mediana de tres ejecuciones seguidas en un contenedor solo
(`build/hito6_plataformas/bench-linux-*.txt`). Entre paréntesis, veces el tiempo real.

| Plataforma | `core` | `video` | `full` (6.000 cuadros) | Hito 5 (`core` · `video` · `full`) |
|---|---:|---:|---:|---|
| macOS arm64 (entrada de rendimiento) | 13.761 (231×) | 3.669 (62×) | 1.875 (31×) | 9.911 · 1.383 · 910 |
| Linux arm64 (VM de Docker) | **12.763** (214×) | **3.258** (55×) | **1.741** (29×) | 9.288 · 1.410 · 872 |
| Linux x86-64 (emulado) | **7.824** (131×) | **2.125** (36×) | **1.143** (19×) | 5.643 · 990 · 636 |
| Windows x86-64 (Wine, emulado, 3.000 cuadros, una medida) | 5.871 (98×) | – | – | 5.218 |

Frente al hito 5, las cifras nuevas ya incluyen el coste del reloj exacto de D47. Aun así, en macOS y en los dos Linux el núcleo
va un 37-39 % más rápido, el vídeo entre ×2,1 y ×2,7 y el juego completo entre ×1,8 y ×2,1: las optimizaciones de D51 se notan
igual fuera de macOS. Las cifras de x86-64 y Wine van bajo la emulación de Apple silicon: dicen que el binario no se arrastra, no
cuánto correría en un PC.

La medida de x86-64 que imprime el propio guion dio 3.610 cuadros/s de núcleo porque se tomó mientras otro contenedor verificaba
las secuencias en arm64. Se descartó y se repitió en serie (la tabla).

## Incidencias del entorno (no del proyecto)

- **El ayudante de credenciales de Docker Desktop se cuelga** desde esta sesión: `docker-credential-desktop get` no vuelve
  (`credsStore: desktop` en `~/.docker/config.json`). El build de arm64 no lo notó porque la imagen base ya estaba en local, pero
  `docker build --platform linux/amd64` se quedó en `load metadata for docker.io/library/debian:trixie-slim` y `docker pull` también
  se colgaba. Reiniciar Docker Desktop no lo arregló.
  **Solución sin tocar la configuración del usuario**: descargar la imagen base sin autenticación con una configuración temporal,
  `DOCKER_CONFIG=<carpeta con {"auths":{}}> DOCKER_HOST=unix://$HOME/.docker/run/docker.sock docker pull --platform linux/amd64 debian:trixie-slim`.
  Después, los guiones resuelven la imagen en local y funcionan tal cual.
- Las dos arquitecturas comparten la etiqueta `sf2recomp-linux`: queda la última que se construyó. Los guiones construyen siempre
  antes de ejecutar, así que no les afecta, pero un `docker run --platform linux/arm64 sf2recomp-linux` a mano después de construir
  la de amd64 falla con «Unable to find image».
- Docker Desktop estaba parado: se arrancó con `open -a Docker` y se paró al terminar (`docker desktop stop`). No se borró ningún
  volumen ni ninguna imagen. Lo único que cambió es que `debian:trixie-slim` apunta ahora a la versión publicada más reciente
  (descargada para amd64 y arm64); las capas de la imagen arm64 del proyecto salieron de la caché sin cambios.

## Comandos

```bash
build/sf2_trace sf2.zip tools/inputs_sf2.txt 12000 1000 macos.wav > macos.txt          # referencia
SF2_OUT=$PWD/build/hito6_plataformas/linux-arm64 tools/linux-docker.sh
PLATFORM=linux/amd64 SF2_OUT=$PWD/build/hito6_plataformas/linux-amd64 tools/linux-docker.sh
SF2_OUT=$PWD/build/hito6_plataformas/wine tools/windows-wine-docker.sh
diff <(cut -c1-45 macos.txt) <(cut -c1-45 build/hito6_plataformas/linux-amd64/trace.txt)   # estado y vídeo
python3 tools/compare_audio.py build/hito6_plataformas/linux-amd64/trace.wav macos.wav --name-a x86-64 --name-b macos
# secuencias con libsf2py.so dentro del contenedor (copia, porque --verify reescribe las marcas)
python3 /src/tools/playgen.py --lib /build/libsf2py.so --roms /roms/sf2.zip --verify /tmp/seq/*.txt
```

## Pendiente

- Lo mismo que en el hito 5: un Windows y un Linux reales con pantalla, audio y mandos. Aquí solo hay contenedores con
  controladores `dummy` y Wine bajo emulación. Tampoco se han medido x86-64 nativo, GCC ≥ 15 ni MSVC.
- Los sanitizadores siguen sin pasarse en Linux (allí habría LeakSanitizer), en Windows y en wasm.
