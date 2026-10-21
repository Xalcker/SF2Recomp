# 2026-10-19 · Hito 5 · Linux y Windows: compilar y probar sin instalar nada global, y qué es igual en todas las plataformas

## Pregunta de partida

Cómo compilar y correr el mismo código en Linux y Windows desde un Mac sin instalar compiladores ni bibliotecas en el sistema, y
cómo saber que la partida es la misma en cada plataforma y no solo que compila.

## Qué se hizo

### Linux con Docker (`tools/linux-docker.sh`, `docker/linux.Dockerfile`)

Imagen de Debian 13 «trixie» con clang 19.1.7, CMake 3.31.6, Ninja, Python 3.13 y `libsdl3-dev` 3.2.10 (no lleva el código ni las
ROMs). El guion monta el repositorio y `sf2.zip` de solo lectura, deja el árbol de compilación en un volumen de Docker
(`sf2recomp-build-<arquitectura>`), compila, pasa `ctest`, calcula las huellas de la partida de 12.000 cuadros y mide.
Docker Desktop estaba instalado y parado; se arrancó para esto y se paró al terminar.

```bash
tools/linux-docker.sh                        # arm64 (nativo en Apple silicon)
PLATFORM=linux/amd64 tools/linux-docker.sh   # x86-64, emulado (Rosetta): lento, pero distinto de arm64 de verdad
```

Las dos plataformas pasan las **siete pruebas** de `ctest` (`unit`, `roms_and_states`, `lockstep_vs_musashi`, `summary_run_vs_step`,
`core_purity`, `game_frames_headless` con los controladores `dummy` de vídeo y audio de SDL, `input_mapping`) y dan el mismo
resumen por cuadro de 12.000 cuadros que macOS (SHA-256 `29339a91…`). Primer fallo en Linux: Musashi (`m68kfpu.c`, solo
validación) usa `sin`/`cos` y exige `-lm` (en macOS va en libSystem); `third_party/CMakeLists.txt` lo enlaza en Unix no Apple.

### Windows con llvm-mingw (`cmake/toolchains/mingw-x86_64.cmake`)

`external/llvm-mingw` (clang 23.1.3, lld, UCRT, para macOS) y `external/sdl3-mingw` (SDL3 3.4.18 de mingw: cabeceras, `SDL3.dll` y
el archivo de importación). La toolchain pone los compiladores, enlaza el runtime de C y C++ dentro del `.exe` y dirige
`find_package(SDL3)` al paquete de mingw.

```bash
tools/setup-local-tools.sh mingw
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake -DSF2_ROM_DIR=$PWD/sf2.zip
cmake --build build-win -j8                       # 16,5 s
```

`build-win/sf2.exe` es un PE32+ de consola x86-64 de 21 MB (con información de depuración) que solo importa `SDL3.dll` (se copia al
lado) y las API de la UCRT; también salen `sf2_unit_tests.exe`, `sf2_rom_tests.exe`, `sf2_trace.exe` y `sf2_input_test.exe`.
La compilación cruzada usa el sub-proyecto nativo (D36) para recompilar el 68000 con las herramientas de macOS.

No hay Windows aquí. **Prueba de humo con Wine dentro de Docker** (`tools/windows-wine-docker.sh`, imagen de Debian x86-64 con
`wine`): `sf2_unit_tests.exe` (633 comprobaciones), `sf2_rom_tests.exe` (7.585, con las ROMs y los estados), `sf2_trace.exe` y
`sf2.exe --bench` corren y pasan. Wine no es Windows (imprime avisos de su propio arranque, como «rosetta error: invalid gdt
selector index 5», que no afectan al programa): vale como evidencia de que el binario y el flujo de archivos, `longjmp` y
SEH de mingw funcionan, no de la ventana, el audio ni los mandos de Windows.

### Igualdad entre plataformas (`tests/trace.c`, D45)

`sf2_trace <roms> <entradas> 12000 1000 salida.wav` corre la partida de validación y, cada 1.000 cuadros, imprime el CRC-32 del
**estado completo** de la máquina (CPU, RAM, registros, vídeo latcheado, placa de sonido con YM2151 y OKI, anillos de muestras), del
cuadro de vídeo y del audio acumulado. Comparado con macOS arm64:

| Plataforma | Estado y vídeo en los 12 puntos | Audio |
|---|---|---|
| Linux arm64 (clang 19) | idénticos | idéntico bit a bit (WAV igual byte a byte, `72f624f2…`) |
| Linux x86-64 (clang 19) | idénticos | 985 de 9.742.081 muestras difieren ≤ 1 LSB, SNR 111-112 dB |
| WebAssembly (Node 24, Emscripten 6.0.13) | idénticos | las mismas 985 muestras, ≤ 1 LSB |
| Windows x86-64 (llvm-mingw, Wine) | idénticos | las mismas 985 muestras, ≤ 1 LSB |

Tramos de `compare_audio.py --sections` (energía ±0,5 dB, bandas ±1,0 dB, LSD ≤ 1,0 dB): los siete pasan en las tres plataformas
sin FMA con diferencia de 0,000 dB en energía y bandas. La explicación es la de D31: arm64 contrae a FMA las expresiones de la
mezcla como el binario de MAME y x86-64 y wasm, con la configuración de compilación por omisión, no; las 985 muestras (0,01 %)
son redondeos de una expresión de float. Que x86-64, wasm y Windows coincidan entre sí en las mismas muestras confirma que
es el FMA y no un error de cada una. Como el estado guardado es idéntico, un `.sta` de una plataforma carga en las demás.

## Restricciones y decisiones (D44)

- **Compilador**: clang ≥ 13 o GCC ≥ 15, porque el código generado necesita `musttail` (`sf2_port.h` lo comprueba al compilar). El GCC
  de Debian 13 es el 14: no vale y el Dockerfile usa clang. No se ha probado con GCC 15; MSVC no tiene el atributo.
- **SDL3 ≥ 3.2**: las llamadas usadas (`SDL_RenderDebugText`, `SDL_GetPathInfo`, `SDL_CreateDirectory`, callbacks de aplicación…)
  existen en 3.2; se probó con 3.2.10 (Debian), 3.4.18 (Homebrew, mingw) y 3.4.2 (puerto de Emscripten).
- Windows arm64: `SF2_MINGW_ARCH=aarch64` selecciona el toolchain de llvm-mingw, pero el paquete de SDL3 para mingw solo trae i686 y
  x86_64; no probado.

## No probado

- Un **Windows real** (ventana, audio de WASAPI, mandos, `SDL_GetPrefPath`), ni 32 bits.
- Un **Linux con pantalla** (X11/Wayland, PulseAudio/PipeWire): en el contenedor solo corren los controladores `dummy`. La carga del
  juego, el bucle, el audio por cola y los archivos sí se ejercitan; el mapeo de ventana y dispositivo no.
- Otras distribuciones, GCC 15 y arquitecturas distintas de arm64 y x86-64 (riscv64, 32 bits, big-endian: el orden de bytes del Z80
  se ha preparado, pero no hay una máquina big-endian para probarlo).
