# 2026-10-19 · Hito 5 · Validación de conjunto y cierre del hito

## Pregunta de partida

Qué queda demostrado al cerrar el hito, con qué comandos, qué sigue sin probar y qué hay que saber para repetirlo.

## Criterio del plan y resultado

> Hecho cuando: el mismo código compila y corre en al menos tres plataformas.

| Plataforma | Compila | Corre el juego | Pruebas | Cómo se construye y prueba |
|---|---|---|---|---|
| **macOS arm64** (nativo) | sí | sí, con ventana, CoreAudio y mandos virtuales | 7 de 7 `ctest` + los `compare-*` de los hitos 2-4 | `make` / `cmake -S . -B build` |
| **Web** (WebAssembly) | sí | sí, en el navegador integrado: arranque, atracción, teclado, estados persistentes, errores | `unit`, `roms_and_states` y `core_purity` en Node; huellas idénticas a macOS | `make web` (`emcmake`), `external/emsdk` |
| **Linux** arm64 y x86-64 | sí (Debian 13, clang 19, SDL 3.2.10) | sin pantalla (controladores `dummy`) | 7 de 7 `ctest`; resumen y estado idénticos a macOS | `tools/linux-docker.sh` |
| **Windows** x86-64 | sí (llvm-mingw cruzado desde macOS) | con Wine (no es Windows) | unitarias, ROMs y estados, huellas idénticas a macOS | `cmake/toolchains/mingw-x86_64.cmake`, `tools/windows-wine-docker.sh` |

macOS y Web corren el juego desde esta máquina; Linux y Windows compilan con un procedimiento documentado y además pasan las pruebas
(Linux en el contenedor, Windows bajo Wine). Lo que **no** se pudo probar está en cada entrada y en la lista de abajo.

## Las comprobaciones de los hitos anteriores siguen pasando

Ejecutadas dos veces: en un árbol de CMake vacío con las referencias de MAME regeneradas de cero y, tras los últimos cambios, a
través del `Makefile` envoltorio sobre el `build/` oficial.

| Objetivo (hito) | Resultado |
|---|---|
| `make check` (1) | 600 cuadros recompilado = Musashi, reloj estimado = reloj de Musashi (diferencia 0), 9.381.759 instrucciones, 494 entregas de IRQ2 |
| `make check-blind` (2) | paso cerrado sobre la partida de 12.000 cuadros: sin divergencia, reloj igual |
| `make compare-mame` (2) | 12.000 cuadros: 11.982 idénticos a MAME y 18 en 13 rachas aisladas de ≤ 3 cuadros, las mismas del hito 2 |
| `make compare-video` (3) | 17 de 17 cuadros idénticos a MAME píxel a píxel; los 17 PNG del juego `sf2` también (`game_frames_headless`) |
| `make compare-sound-cmds` (4) | las 1.391 escrituras del 68000 coinciden en latch, valor y cuadro |
| `make compare-audio` (4) | accesos del Z80 al ciclo, YM2151, OKI y mezcla bit a bit; WAV = WAV de MAME (`-wavwrite`), siete tramos con diferencia 0 |
| `make png-video`, `wav-recomp`, `run-audio`, `analyze`, `dasmcheck` | como antes (`dasmcheck` sigue informando las 10 diferencias cosméticas de longitud del hito 1, que no hacen fallar el objetivo) |

Huellas del Makefile original frente a CMake (resumen de 12.000 cuadros, WAV, comandos de sonido, 17 PNG): idénticas byte a byte
(ver la entrada de CMake).

## Pruebas nuevas (`ctest --test-dir build`, 7)

`unit` (633 comprobaciones), `roms_and_states` (7.585), `lockstep_vs_musashi`, `summary_run_vs_step`, `core_purity`,
`game_frames_headless`, `input_mapping` (10). Más `sf2_trace` (huellas entre plataformas) y ASan/UBSan sobre la partida completa
(sin avisos). En wasm: `ctest --test-dir build-web` (3 pruebas en Node).

## Entregables del encargo

1. **CMake** en la raíz y por carpeta, SDL3 como única dependencia externa de plataforma, el recompilador como paso de la
   compilación sobre `SF2_ROM_DIR`, Makefile como envoltorio con el mismo resultado → `2026-10-19-hito5-cmake.md`, D36.
2. **Capa de plataforma** separada del núcleo (sin llamadas al sistema, comprobado), auditoría de portabilidad y aserciones
   estáticas → `…-plataforma-y-nucleo.md`, D37, D39.
3. **Configuración** (dipswitches, controles de teclado y mando, escala, volumen), **estados** guardados y cargados, **ROMs** desde
   carpeta o zip con CRC y mensajes → `…-configuracion-y-estados.md`, D38, D40-D42.
4. **Plataformas** macOS, Web (Emscripten en `external/emsdk`, selector de `sf2.zip`, sin ROMs embebidas), Linux (Docker) y Windows
   (llvm-mingw) con procedimiento y lo no probado → `…-web.md`, `…-otras-plataformas.md`, D43-D45.
5. **Rendimiento** del núcleo sin vídeo ni sonido y con ellos en macOS y Web → `…-rendimiento.md`.
6. **Documentación**: esta serie de entradas, `README.md` raíz con la construcción por plataforma, un README en `app/`, `platform/`,
   `tests/` y los que cambian (`runtime/`, `harness/`, `tools/`, `recompiler/`), D36-D45 y `docs/PLAN.md`.

## Qué cambió en el árbol

- Nuevo: `CMakeLists.txt` (y uno por carpeta), `cmake/` (pipeline, generación, toolchain de Windows), `runtime/` (`sf2_port.h`, `crc32`,
  `zipread`, `romset`, `png`, `state`, `machine` —movida desde `harness/`—, `dispatch`, `sf2`), `platform/` (`hostio`, `config`, `wav`
  movido), `app/` (`main.c`, `input.c`, `web/shell.html`), `tests/`, `docker/`, `tools/setup-local-tools.sh`, `tools/linux-docker.sh`,
  `tools/windows-wine-docker.sh`, `recompiler/host/`.
- Cambiado: `Makefile` (envoltorio), `tools/interleave.py` (zip o carpeta, por CRC), `recompiler/gen_c.py` (emite `recomp_rom_crc32`),
  `harness/` (`blind.c` sobre la máquina del núcleo, `--step`, `--roms`; `harness.c` nuevo; sin `machine.c` ni `view.c`), y en el núcleo
  `gfx`, `sound`, `video`, `cps1`, `mem`, `oki6295`, `z80.h`, `ym2151.cpp` (ver la entrada de plataforma).
- Desaparece: `harness/view.c` (el visor `sf2_video`, sustituido por `sf2`) y `harness/machine.c/.h`.
- Instalado fuera del repositorio versionado pero dentro del proyecto: `external/cmake`, `external/emsdk`, `external/llvm-mingw`,
  `external/sdl3-mingw`.

## Pendiente y no probado

Lo que no se ha podido comprobar en esta máquina (cada entrada lo detalla):

- **Windows real** (ventana, WASAPI, mandos, carpeta de preferencias) y Windows arm64; **Linux con pantalla** (X11/Wayland) y otras
  distribuciones; **Firefox y Safari**, móviles y mandos en el navegador; **GCC ≥ 15**; máquinas big-endian y de 32 bits.
- La partida completa con el teclado en el navegador (por la baja cadencia del navegador integrado bajo presión de memoria) y con el
  teclado nativo (SDL no permite inyectar teclas; se probó el mapeo por nombres y los mandos con dispositivos virtuales).
- Medida del rendimiento en la web con presentación libre y en un PC x86-64 real (solo hay emulación).
- Empaquetado (bundle de macOS, instalador de Windows, `.deb`, `.zip` de la página).

Para los hitos siguientes: el hito 6 (cobertura y robustez) eliminar la red de seguridad de Musashi —el juego, sin ella, se detiene con
un mensaje si llega a código que el recompilador no vio, y en 12.000 cuadros no ha ocurrido—, optimizar el vídeo si hiciera falta, y
recorrer todos los personajes y rutas; el reloj exacto del 68000 sigue pendiente (D16, D29).
