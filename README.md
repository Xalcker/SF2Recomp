# SF2Recomp

> **Nota sobre el desarrollo:** este proyecto se está creando con ayuda de inteligencia artificial (Claude, de Anthropic). El
> código, las herramientas y la documentación se desarrollan de forma asistida por IA, con revisión y verificación humanas
> (por ejemplo, comparando cuadro a cuadro contra MAME).

> **English summary:** SF2Recomp statically recompiles the Motorola 68000 program of *Street Fighter II: The World Warrior*
> (Capcom CPS1, World 910522) into portable C, in the spirit of N64Recomp. It runs natively on macOS, Linux, Windows and the
> browser (WebAssembly), and is frame-exact against MAME 0.289 on fourteen full playthroughs. **No ROMs or Capcom assets are
> included**: you supply your own `sf2.zip` (MAME `sf2` set), and the recompiler generates the C code at build time on your
> machine. Build instructions are below (in Spanish); `make ROMDIR=/path/to/sf2.zip` is the short version. This project is
> developed with AI assistance (Claude). MIT licensed, excluding anything derived from the ROMs.

Recompilación estática del programa 68000 de *Street Fighter II: The World Warrior* (Capcom CPS1, World 910522) a C
portable, al estilo de N64Recomp. El jugador aporta sus propias ROMs: el repositorio solo contiene herramientas,
recompilador, núcleo, aplicación y documentación (`docs/DECISIONES.md`, D5).

Estado: **hito 6 completado** (cobertura y robustez). El mismo código compila y corre el juego completo en **macOS** (arm64),
**Linux** (arm64 y x86-64), **Windows** (x86-64, compilación cruzada) y el **navegador** (WebAssembly), con configuración,
dipswitches, remapeo de teclado y mando, estados guardados y carga de las ROMs desde `sf2.zip` o una carpeta. Catorce partidas
que recorren el juego entero (los ocho personajes hasta el final con los cuatro jefes, bonus, continuar, fin de partida, dos
jugadores, retador, menú de pruebas y varios dipswitches; 603.050 cuadros) son **idénticas a MAME 0.289 en todos los cuadros**,
en los comandos de sonido y en las capturas comparadas, gracias al reloj exacto del 68000 de MAME; ejecutan el 73,8 % del código
sin un solo salto a código no recompilado. Sin avisos de ASan/UBSan, sin fugas ni crecimiento de memoria en una hora de juego y
con estados que reproducen la partida exacta. Hoja de ruta y estado del proyecto en `docs/PLAN.md`; cada paso tiene su entrada
en `docs/bitacora/`.

## Qué necesitas

- **Tus ROMs**: `sf2.zip` del set `sf2` de MAME (World 910522, 23 archivos de ROM; los 8 volcados de PLD que trae no se
  usan) o una carpeta con los 23 archivos sueltos (nombres de MAME, actuales o antiguos). Se identifican por CRC-32, así que
  el nombre de los archivos dentro del zip da igual; si falta o sobra algo, el juego dice qué.
- clang ≥ 13 (o GCC ≥ 15; GCC 14 no vale: falta `musttail`), CMake ≥ 3.20, Python 3 (sin paquetes) y SDL3 ≥ 3.2.
- El recompilador **corre durante la compilación** sobre tus ROMs (`SF2_ROM_DIR`) y genera el C del programa de Capcom en
  el árbol de compilación. Ese código, y todo ejecutable o `.wasm` que lo contiene, **deriva de las ROMs: son para tu uso
  personal y no se publican** (D5, D43).

## Construir y jugar

### macOS (arm64)

```bash
brew install sdl3                      # una vez; o compila SDL3 donde prefieras y pasa -DCMAKE_PREFIX_PATH=
tools/setup-local-tools.sh cmake       # opcional: CMake local en external/ (si no, el del sistema)
make ROMDIR=/ruta/a/sf2.zip -j8        # recompila, compila (~20 s) y deja build/sf2
build/sf2 /ruta/a/sf2.zip              # a jugar (la ruta se recuerda en la configuración si pones [paths] rom)
```

El `Makefile` es un envoltorio de CMake; sin él:

```bash
cmake -S . -B build -DSF2_ROM_DIR=/ruta/a/sf2.zip
cmake --build build -j8
```

### Linux

En Debian 13 / Ubuntu con SDL3 en los paquetes (`libsdl3-dev`):

```bash
sudo apt install build-essential clang cmake ninja-build python3 libsdl3-dev
CC=clang CXX=clang++ cmake -S . -B build -G Ninja -DSF2_ROM_DIR=/ruta/a/sf2.zip
cmake --build build && build/sf2 /ruta/a/sf2.zip
```

Sin instalar nada en la máquina: `tools/linux-docker.sh` construye una imagen de Debian 13 (`docker/linux.Dockerfile`),
compila el proyecto con el repositorio y `sf2.zip` montados de solo lectura, pasa `ctest` y mide (`PLATFORM=linux/amd64`
para x86-64, emulado en Apple silicon; `SF2_ROM_DIR=... ` para otra ruta de ROMs). Para jugar hace falta una instalación
con SDL3 y pantalla; el contenedor corre con los controladores de vídeo y audio `dummy`.

### Windows (compilación cruzada desde macOS o Linux)

```bash
tools/setup-local-tools.sh mingw       # llvm-mingw + SDL3 para mingw en external/ (SHA-256 verificados)
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake -DSF2_ROM_DIR=/ruta/a/sf2.zip
cmake --build build-win -j8            # build-win/sf2.exe + SDL3.dll (es lo único que necesita, junto al runtime de Windows)
```

`tools/windows-wine-docker.sh` ejecuta las pruebas y una medida de esos `.exe` con Wine dentro de Docker (prueba de
humo, no sustituye a Windows). **No se ha probado en un Windows real.** Compilar con Visual Studio no está soportado
(no tiene `musttail`).

### Web (WebAssembly)

```bash
tools/setup-local-tools.sh emsdk       # Emscripten 6.0.13 en external/emsdk (~1,8 GB)
make web ROMDIR=/ruta/a/sf2.zip        # o: emcmake cmake -S . -B build-web -DSF2_ROM_DIR=... && cmake --build build-web
python3 -m http.server -d build-web 8000     # y abre http://localhost:8000/index.html
```

La página te pide tu `sf2.zip` con un selector de archivo (o arrastrándolo); lo lee tu navegador, no se envía a ningún
sitio y no hay ROMs embebidas. La configuración y los estados guardados quedan en IndexedDB. Probado en el navegador
integrado de la aplicación de escritorio de Claude (Chromium); Firefox y Safari **no probados** (necesitan llamadas en cola
y excepciones de wasm: Firefox 121+, Safari 18.2+). El audio empieza tras el primer gesto del usuario (el clic en el
selector).

## Jugar

| | Teclado | Mando (SDL) |
|---|---|---|
| Jugador 1 | flechas · **Z X C** puñetazos · **A S D** patadas · **5** moneda · **1** start | cruceta o stick · X Y R1 · A B R2 · Back · Start |
| Jugador 2 | **I J K L** · **R T Y** · **F G H** · **6** · **2** | mando 2, igual |
| Estados | **F5** guardar, **F7** cargar, **F6/F8** ranura (0-9) | |
| Menú | **M** (o el botón guía del mando) abre el menú del juego: ajuste rápido (original, 16:9, ultraancho), pantalla ancha y mods, **entrenamiento**, vídeo, sonido y controles; Enter acepta y Esc vuelve (`docs/MENU.md`) | |
| Entrenamiento | **7** modo entrenamiento · **8** visor de entradas · **9** cajas de golpe (`docs/ENTRENAMIENTO.md`) | |
| Otros | **P** pausa · **Tab** avance rápido · **F9** pantalla ancha · **F10** reinicio · **F11** o Alt+Intro pantalla completa · **F12** captura · **Esc** salir | |

La configuración (`sf2recomp.ini`, en la carpeta de preferencias; en el navegador, editable en la propia página) se crea la
primera vez con todos los valores y comentarios: escala y proporción de la ventana, volumen y frecuencia de salida del
audio, dipswitches con nombre (dificultad 0-7, precio por moneda, juego gratis, continuar, sonidos de la demostración...),
remapeo de cada tecla y botón y las teclas rápidas. Detalle en `platform/README.md` y `app/README.md`.

## Comprobaciones

```bash
make test                  # ctest: pruebas unitarias, con ROMs y estados guardados, paso cerrado con Musashi, pureza del
                           # núcleo, juego sin ventana frente al arnés, fallo de despacho provocado, mandos virtuales (8 pruebas)
make check                 # 600 cuadros del código recompilado contra Musashi (hito 1)
make compare-mame          # partida a ciegas de 12.000 cuadros contra MAME (hito 2; necesita el MAME local de external/)
make compare-video         # 17 cuadros píxel a píxel contra MAME (hito 3)
make compare-sound-cmds    # comandos de sonido del 68000 por cuadro contra MAME (hito 4)
make compare-audio         # accesos del Z80 al ciclo, chips y mezcla bit a bit, WAV por energía y espectro (hito 4)
make coverage              # las 14 secuencias de tools/sequences/ sin red de seguridad + informe de cobertura (hito 6)
make compare-coverage      # las 14 secuencias contra MAME: resumen por cuadro, comandos de sonido y capturas (hito 6)
```

Los `compare-*` exigen además `tools/mame.sh` (MAME 0.289 compilado solo con el driver CPS1 en `external/mame`, ver
`docs/bitacora/2026-10-19-hito0-mame-local.md`) y `sf2.zip` en la raíz. `BFRAMES=N`, `INPUTS=archivo` y `VFRAMES=a,b,c`
cambian la partida y los cuadros. Más del hito 6: `python3 tools/coverage.py --roundtrip` (guardar y cargar a mitad de cada
secuencia), `python3 tools/playgen.py --verify tools/sequences/*.txt` (CRC de los estados finales) y un árbol con sanitizadores
(`cmake -S . -B build-asan -DSF2_SANITIZE=address,undefined`). Las secuencias nuevas se generan con `tools/playgen.py`
(`tools/sequences/README.md`).

## Entrenamiento

`build/sf2 --training` enciende un modo de entrenamiento (sin límite de tiempo, barras que se recuperan, J2 como dummy con guion, visor de entradas y
cajas de golpe; teclas `7`/`8`/`9`, o la página *Entrenamiento* del menú **M**). Apagado, el juego sigue siendo idéntico a MAME. Detalles en `docs/ENTRENAMIENTO.md`.

## Estructura

```
CMakeLists.txt, cmake/     sistema de construcción (CMake en cada carpeta); cmake/toolchains/: Windows con llvm-mingw
sf2/, sf2.zip              ROMs del usuario (no se versionan)
tools/                     intercalado de ROMs, análisis, MAME local, scripts Lua y comparadores, guiones de instalación y de Docker,
                           jugador automático (playgen.py), cobertura (coverage.py) y las secuencias (tools/sequences/)
recompiler/                generador de C (68000 → C) y su sub-proyecto nativo para compilaciones cruzadas
runtime/                   el núcleo, sin llamadas al sistema: CPU, memoria, máquina, vídeo, sonido, ROMs, estados (sf2.h)
platform/                  archivos, ROMs desde zip o carpeta, PNG, WAV y configuración (sin SDL)
app/                       el juego: SDL3 (ventana, teclado, mandos, audio) y la página web (app/web)
harness/                   arneses de validación con Musashi (sf2_lockstep, sf2_blind)
tests/                     pruebas de ctest
third_party/               Musashi (MIT, solo validación) y ymfm (BSD-3, YM2151)
docker/                    imágenes de Linux y de Wine para las pruebas (sin código ni ROMs)
external/                  herramientas locales: MAME, CMake, Emscripten, llvm-mingw (no se versiona)
docs/                      plan, decisiones, referencias, revisiones y bitácora
build/, build-web/, build-win/   árboles de compilación (no se versionan)
```

## Rendimiento

Cuadros por segundo (la partida de 12.000 cuadros de `tools/inputs_sf2.txt`; 59,64 = tiempo real). macOS: tras las
optimizaciones del hito 6, que no cambian ni un bit de ningún resultado (`docs/bitacora/2026-10-19-hito6-rendimiento.md`);
web: hito 5 (`docs/bitacora/2026-10-19-hito5-rendimiento.md`), no se ha vuelto a medir:

| | núcleo solo | + vídeo | + vídeo, sonido y ventana |
|---|---:|---:|---:|
| macOS arm64 (nativo, hito 6) | 13.761 | 3.669 | 1.875 |
| WebAssembly (Chromium, hito 5) | 6.730 | 1.058 | 511 |

## Documentación

`docs/PLAN.md` (hitos), `docs/DECISIONES.md` (D1-D52), `docs/bitacora/` (una entrada por paso), un `README.md` en cada carpeta.

## Contribuir

- Antes de enviar cambios, corre `make test` (necesita tus ROMs en `ROMDIR`).
- **Nunca subas ROMs, ni código generado a partir de ellas, ni capturas del juego** (`sf2/`, `generated/`, `*.bin`, `*.zip` y
  `docs/capturas-*/` están en `.gitignore` por esto; ver `docs/DECISIONES.md`, D5).
- Los mods deben poder apagarse y dejar el juego idéntico a MAME.
- Cada paso importante se anota en `docs/bitacora/`.

## Licencia

El código de este repositorio se publica bajo la licencia [MIT](LICENSE). Los componentes de `third_party/` (Musashi, ymfm)
conservan sus propias licencias. Esta licencia no cubre las ROMs ni nada derivado de ellas, que pertenecen a Capcom.
