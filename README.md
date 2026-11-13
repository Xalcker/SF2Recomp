# SF2Recomp

> **Nota sobre el desarrollo:** este proyecto se está creando con ayuda de inteligencia artificial (Claude, de Anthropic). El
> código, las herramientas y la documentación se desarrollan de forma asistida por IA, con revisión y verificación humanas
> (por ejemplo, comparando cuadro a cuadro contra MAME).

> **English summary:** SF2Recomp statically recompiles the Motorola 68000 program of Capcom's CPS1 *Street Fighter II* games
> into portable C, in the spirit of N64Recomp: *The World Warrior* (World 910522), *Champion Edition* (World 920513) and *Hyper
> Fighting* (World 921209). It runs natively on macOS, Linux, Windows and the browser (WebAssembly) and is frame-exact against
> MAME 0.289 (fourteen full playthroughs of *The World Warrior*; attract mode, a scripted game and audio for the other two, which
> have only been tested on macOS; all three also build and pass the tests natively on Windows with MSYS2).
> **No ROMs or Capcom assets are included**: you supply your own MAME zip (`sf2.zip`, `sf2ce.zip` or `sf2hf.zip`), and the
> recompiler generates the C code at build time on your machine. Build instructions are below (in Spanish); `make
> ROMDIR=/path/to/sf2.zip` is the short version, and `make GAME=sf2ce BUILD=build-ce ROMDIR=/path/to/sf2ce.zip` builds
> *Champion Edition*. This project is developed with AI assistance (Claude). GPL-2.0 licensed, excluding anything derived from
> the ROMs.

Recompilación estática del programa 68000 de los *Street Fighter II* de la CPS1 de Capcom a C portable, al estilo de
N64Recomp: *The World Warrior* (World 910522), *Champion Edition* (World 920513) y *Hyper Fighting* (World 921209). El
jugador aporta sus propias ROMs: el repositorio solo contiene herramientas, recompilador, núcleo, aplicación y documentación
(`docs/DECISIONES.md`, D5).

Estado: **hito 6 completado** (cobertura y robustez). El mismo código compila y corre *The World Warrior* completo en **macOS** (arm64),
**Linux** (arm64 y x86-64), **Windows** (x86-64, nativo con MSYS2 o por compilación cruzada) y el **navegador** (WebAssembly), con configuración,
dipswitches, remapeo de teclado y mando, estados guardados y carga de las ROMs desde `sf2.zip` o una carpeta. Catorce partidas
que recorren el juego entero (los ocho personajes hasta el final con los cuatro jefes, bonus, continuar, fin de partida, dos
jugadores, retador, menú de pruebas y varios dipswitches; 603.050 cuadros) son **idénticas a MAME 0.289 en todos los cuadros**,
en los comandos de sonido y en las capturas comparadas, gracias al reloj exacto del 68000 de MAME; ejecutan el 73,8 % del código
sin un solo salto a código no recompilado. Sin avisos de ASan/UBSan, sin fugas ni crecimiento de memoria en una hora de juego y
con estados que reproducen la partida exacta. Hoja de ruta y estado del proyecto en `docs/PLAN.md`; cada paso tiene su entrada
en `docs/bitacora/`.
Después del hito 6 se añadieron los mods (pantalla ancha, ultraancho, HFR, entrenamiento): ver «Mods y mejoras» y `docs/MODS.md`.
Y después, *Champion Edition* y *Hyper Fighting* (ver «Variantes»).

## Variantes

Cada compilación mueve un juego: el C generado es de un programa concreto. Se elige con `GAME` (`-DSF2_GAME=` en CMake); conviene
una carpeta de compilación por juego (`BUILD=`). Detalle, fases y límites en `docs/VARIANTES.md`.

| Juego | Set de MAME | `GAME` | Estado |
|---|---|---|---|
| *The World Warrior* (World 910522) | `sf2` | `sf2` (por omisión) | completo: 14 partidas idénticas a MAME, todos los mods |
| *Champion Edition* (World 920513) | `sf2ce` | `sf2ce` | idéntico a MAME en atracción, en una partida con entradas y en audio; mods salvo las herramientas de desarrollo |
| *Hyper Fighting* (World 921209) | `sf2hf` | `sf2hf` | igual que *Champion Edition* |

En *Champion Edition* y *Hyper Fighting* se ha ejecutado alrededor del 31 % del código (en *The World Warrior*, el 73,8 %): en
partes no recorridas todavía (más personajes, jefes, finales) el juego puede pararse con un «salto a código que el recompilador no
vio»; el mensaje dice qué pista añadir en `tools/hints_<set>.txt` y el estado de rescate permite repetirlo. Si te pasa, abre una
incidencia con la dirección.

## Qué necesitas

- **Tus ROMs**, del set de MAME del juego que quieras compilar: `sf2.zip` (*The World Warrior*, 23 archivos de ROM), `sf2ce.zip`
  (*Champion Edition*, 18) o `sf2hf.zip` (*Hyper Fighting*, 18), o una carpeta con los archivos sueltos (nombres de MAME,
  actuales o antiguos). Los volcados de PLD que traen los zip no se usan. Las ROMs se identifican por CRC-32, así que el nombre
  de los archivos dentro del zip da igual; si falta o sobra algo, el juego dice qué.
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

Para *Champion Edition* o *Hyper Fighting*, lo mismo con `GAME` y otra carpeta de compilación:

```bash
make GAME=sf2ce BUILD=build-ce ROMDIR=/ruta/a/sf2ce.zip -j8
build-ce/sf2 /ruta/a/sf2ce.zip         # sin ruta busca sf2ce.zip junto al juego (o sf2hf.zip en la de HF)
```

El `Makefile` es un envoltorio de CMake; sin él:

```bash
cmake -S . -B build -DSF2_ROM_DIR=/ruta/a/sf2.zip          # y -DSF2_GAME=sf2ce o sf2hf para las otras variantes
cmake --build build -j8
```

Las instrucciones de Linux, Windows y Web de abajo son las de *The World Warrior*; las demás variantes se compilan igual añadiendo
`-DSF2_GAME=` (o `GAME=` en `make web`), pero **solo se han probado en macOS** y en Windows nativo (las tres variantes, `ctest` 8/8).
Además se puede compilar *Street Fighter Zero* en la versión de CPS Changer (`-DSF2_GAME=sfzch` con `sfzch.zip`): es jugable en
vanilla y sin mods, e idéntico a MAME en estado, vídeo y audio (`docs/VARIANTES.md`, D71).

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

### Windows

**Nativo, con MSYS2** (probado en Windows 11 x86-64 con las tres variantes: `ctest` 8/8). Hace falta el GCC del entorno UCRT64 de
[MSYS2](https://www.msys2.org/) (`pacman -S mingw-w64-ucrt-x86_64-gcc`; es GCC ≥ 15), CMake, Ninja y Python 3 (los de `winget` valen:
`Kitware.CMake`, `Ninja-build.Ninja`, `Python.Python.3.13`) y las bibliotecas de SDL3 para MinGW, que el guion deja en `external/`.
Desde Git Bash o la consola de MSYS2, en la carpeta del proyecto:

```bash
tools/setup-local-tools.sh sdl3-mingw              # SDL3 3.4.18 para MinGW en external/sdl3-mingw (SHA-256 verificado)
export PATH="/c/msys64/ucrt64/bin:$PATH"           # gcc y g++ de MSYS2 (solo para compilar)
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_PREFIX_PATH="$PWD/external/sdl3-mingw/x86_64-w64-mingw32" -DSF2_ROM_DIR=C:/ruta/a/sf2.zip
cmake --build build                                # build/sf2.exe + SDL3.dll
build/sf2.exe C:/ruta/a/sf2.zip
```

Si CMake no encuentra Python, indícalo con `-DPython3_EXECUTABLE=C:/ruta/a/python.exe`. El `.exe` lleva dentro el runtime de GCC: solo
necesita `SDL3.dll` a su lado, y se puede lanzar desde el Explorador o PowerShell sin MSYS2 en el `PATH`. El `Makefile` no se usa en
Windows (es de macOS y Linux): se llama a CMake directamente, y las otras variantes se eligen con `-DSF2_GAME=sf2ce` o `sf2hf` y otra
carpeta (`-B build-hf`). El clang de LLVM para Windows (`x86_64-pc-windows-msvc`) no se ha probado, y Visual Studio no está soportado
(no tiene `musttail`). No se puede recompilar con el juego abierto: Windows no deja sobrescribir un `.exe` en uso.

**Compilación cruzada desde macOS o Linux:**

```bash
tools/setup-local-tools.sh mingw       # llvm-mingw + SDL3 para mingw en external/ (SHA-256 verificados)
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake -DSF2_ROM_DIR=/ruta/a/sf2.zip
cmake --build build-win -j8            # build-win/sf2.exe + SDL3.dll (es lo único que necesita, junto al runtime de Windows)
```

`tools/windows-wine-docker.sh` ejecuta las pruebas y una medida de esos `.exe` con Wine dentro de Docker (prueba de
humo, no sustituye a Windows). Los `.exe` de la compilación cruzada **no se han probado en un Windows real**; la compilación nativa
de arriba, sí.

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
| Estados | **F5** guardar, **F7** cargar, **F6/F8** ranura (0-9); cada variante tiene sus ranuras | |
| Menú | **M** (o el botón guía del mando) abre el menú del juego: ajuste rápido (original, 16:9, ultraancho), pantalla ancha y mods, **entrenamiento**, vídeo, sonido y controles; Enter acepta y Esc vuelve (`docs/MENU.md`) | |
| Entrenamiento | **7** modo entrenamiento · **8** visor de entradas · **9** cajas de golpe (`docs/ENTRENAMIENTO.md`) | |
| Otros | **P** pausa · **Tab** avance rápido · **F9** pantalla ancha · **F10** reinicio · **F11** o Alt+Intro pantalla completa · **F12** captura · **Esc** salir | |

Los controles son los mismos en las tres variantes.

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
`docs/bitacora/2026-10-19-hito0-mame-local.md`) y el zip (o la carpeta) del set en la raíz. Con `GAME=sf2ce` o `GAME=sf2hf` (y su
`BUILD` y `ROMDIR`) comparan esa variante: `make compare-mame compare-sound-cmds GAME=sf2ce BUILD=build-ce ROMDIR=sf2ce`.
`coverage` y `compare-coverage` usan las secuencias de `tools/sequences/`, que son de *The World Warrior*. `BFRAMES=N`, `INPUTS=archivo` y `VFRAMES=a,b,c`
cambian la partida y los cuadros. Más del hito 6: `python3 tools/coverage.py --roundtrip` (guardar y cargar a mitad de cada
secuencia), `python3 tools/playgen.py --verify tools/sequences/*.txt` (CRC de los estados finales) y un árbol con sanitizadores
(`cmake -S . -B build-asan -DSF2_SANITIZE=address,undefined`). Las secuencias nuevas se generan con `tools/playgen.py`
(`tools/sequences/README.md`).

## Mods y mejoras

Además de correr el juego tal cual, el port trae mods que se activan desde el menú **M**, con teclas, con opciones de línea de comandos o en `sf2recomp.ini`. **Apagados, el juego es idéntico a MAME.**
Solo presentación: **pantalla ancha** real (512 px = 16:9 sin estirar, hasta 800), **HUD en los bordes** y **HFR** (120/144/240 Hz por interpolación, sin acelerar el juego). Cambian el juego: **lógica ancha**, **stage completo** para monitores ultraanchos
(`mods/ultrawide-3440.ini`), las **herramientas de desarrollo de Capcom** (`--dev-tool`) y el **modo entrenamiento** (`--training`: sin límite de tiempo, barras que se recuperan, J2 como dummy con guion, visor de entradas y cajas de golpe; teclas `7`/`8`/`9`).
Catálogo y cómo activar cada uno en `docs/MODS.md`; el menú en `docs/MENU.md`; detalle en `docs/WIDESCREEN.md`, `docs/ULTRAWIDE.md`, `docs/HFR.md`, `docs/ESTEREO.md` y `docs/ENTRENAMIENTO.md`.
*Champion Edition* y *Hyper Fighting* tienen todos salvo las herramientas de desarrollo, que son propias de *The World Warrior*; sus
límites están en `docs/VARIANTES.md`.

## Estructura

```
CMakeLists.txt, cmake/     sistema de construcción (CMake en cada carpeta); cmake/toolchains/: Windows con llvm-mingw
sf2/, sf2ce/, sf2hf/, *.zip  ROMs del usuario (no se versionan)
tools/                     intercalado de ROMs, análisis, MAME local, scripts Lua y comparadores, guiones de instalación y de Docker,
                           jugador automático (playgen.py), cobertura (coverage.py), las secuencias (tools/sequences/), las variantes
                           (games.py, mame_game.lua) y las pistas del análisis de cada una (hints.txt, hints_sf2ce.txt, hints_sf2hf.txt)
recompiler/                generador de C (68000 → C), su sub-proyecto nativo para compilaciones cruzadas y los parches de los mods de
                           cada variante (mod_patches.txt, mod_patches_sf2ce.txt, mod_patches_sf2hf.txt)
runtime/                   el núcleo, sin llamadas al sistema: CPU, memoria, máquina, vídeo, sonido, ROMs, estados, mod de entrenamiento (sf2.h)
                           y el descriptor de cada variante (game.c: reloj, CPS-B, mapeador, ROMs, RAM de los mods)
platform/                  archivos, ROMs desde zip o carpeta, PNG, WAV y configuración (sin SDL)
app/                       el juego: SDL3 (ventana, teclado, mandos, audio, menú y visores) y la página web (app/web)
harness/                   arneses de validación con Musashi (sf2_lockstep, sf2_blind)
tests/                     pruebas de ctest
third_party/               Musashi (MIT, solo validación) y ymfm (BSD-3, YM2151)
docker/                    imágenes de Linux y de Wine para las pruebas (sin código ni ROMs)
external/                  herramientas locales: MAME, CMake, Emscripten, llvm-mingw (no se versiona)
docs/                      plan, decisiones, mods, menú, referencias, revisiones y bitácora
mods/                      perfiles .ini para monitores ultraanchos (--config mods/ultrawide-3440.ini)
build/, build-web/, build-win/, build-ce/, build-hf/   árboles de compilación (no se versionan)
```

## Rendimiento

Cuadros por segundo (la partida de 12.000 cuadros de `tools/inputs_sf2.txt`; 59,64 = tiempo real). macOS: tras las
optimizaciones del hito 6, que no cambian ni un bit de ningún resultado (`docs/bitacora/2026-10-19-hito6-rendimiento.md`);
web: hito 5 (`docs/bitacora/2026-10-19-hito5-rendimiento.md`), no se ha vuelto a medir:

| | núcleo solo | + vídeo | + vídeo, sonido y ventana |
|---|---:|---:|---:|
| macOS arm64 (nativo, hito 6) | 13.761 | 3.669 | 1.875 |
| WebAssembly (Chromium, hito 5) | 6.730 | 1.058 | 511 |

Solo *The World Warrior*: *Champion Edition* y *Hyper Fighting* no se han medido (su 68000 va a 12 MHz, así que el núcleo ejecuta más
instrucciones por cuadro).

## Documentación

`docs/README.md` es el índice: `docs/PLAN.md` (hitos), `docs/VARIANTES.md` (*Champion Edition*, *Hyper Fighting* y *Street Fighter Zero* de CPS Changer), `docs/MODS.md` (mods y mejoras), `docs/MENU.md` (menú del juego), `docs/DECISIONES.md` (D1-D73), `docs/bitacora/` (una entrada por paso), un `README.md` en cada carpeta.

## Contribuir

- Antes de enviar cambios, corre `make test` (necesita tus ROMs en `ROMDIR`); si tocas el núcleo, el recompilador o el análisis, en
  cada variante que tengas (`GAME=`), y comprueba que *The World Warrior* sigue idéntico a MAME.
- **Nunca subas ROMs, ni código generado a partir de ellas, ni capturas del juego** (`sf2/`, `sf2ce/`, `sf2hf/`, `generated/`,
  `*.bin`, `*.zip` y `docs/capturas-*/` están en `.gitignore` por esto; ver `docs/DECISIONES.md`, D5).
- Los mods deben poder apagarse y dejar el juego idéntico a MAME.
- Cada paso importante se anota en `docs/bitacora/`.

## Licencia

El código de este repositorio se publica bajo la licencia [GPL-2.0](LICENSE). Los componentes de `third_party/` (Musashi, ymfm)
conservan sus propias licencias. Esta licencia no cubre las ROMs ni nada derivado de ellas, que pertenecen a Capcom.
