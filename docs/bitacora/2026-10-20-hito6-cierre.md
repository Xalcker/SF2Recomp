# 2026-10-20 · Hito 6 · Cierre: cobertura completa del juego y robustez del ejecutable

## Pregunta de partida

¿Se cumple lo que el hito pedía? Que todas las secuencias lleguen al final sin fallos de despacho ni divergencias sostenidas con
MAME, que el informe de cobertura y los resultados de los sanitizadores estén en la bitácora y que las comprobaciones de los
hitos anteriores sigan pasando.

## Criterios

| Criterio | Resultado | Dónde |
|---|---|---|
| Secuencias que recorren el juego (8 personajes hasta el final con jefes y final, bonus, continuar y fin de partida, 2 jugadores, retador, menú de pruebas, dipswitches) | 14 secuencias, 603.050 cuadros, formato documentado | `tools/sequences/`, entrada de secuencias |
| `make coverage` con informe dinámico | 73,8 % de las instrucciones, 81,2 % de las funciones; 0 destinos despachados sin enumerar | `build/coverage/informe_cobertura.md`, entrada de cobertura |
| Todas hasta el final sin fallos de despacho | 0 fallos (sin red de seguridad, también con ASan) | ídem |
| Sin divergencias sostenidas con MAME | **0 cuadros distintos** en las 14, comandos de sonido y 814 capturas idénticos | `build/coverage/informe_mame.md` |
| Correcciones en recompilador, heurísticas, máquina e `hints.txt` | 4 fallos del análisis, 7 `table_end`, `inline_text`, reloj exacto del 68000 | entradas de correcciones y del reloj; D47, D49 |
| Fallo de despacho con mensaje claro | stderr, pantalla, `errores.log`, estado de rescate; prueba `game_dispatch_failure` | D46 |
| ASan + UBSan en `make test` y las secuencias | limpio en nuestro código; un comportamiento indefinido en Musashi, parcheado | entrada de sanitizadores; D52 |
| Ejecución larga ≥ 30 min sin fugas ni crecimiento | 220.000 cuadros (61 min), huella física plana en 24,5 MB, 0 fugas | ídem |
| Guardar/cargar a mitad de secuencia da el mismo resumen | 586 puntos, 14 de 14 con resumen y WAV idénticos | `build/coverage/informe_estados.md` |
| Rendimiento sin cambiar resultados | núcleo +19 %, vídeo ×2,03, juego completo +54 %; todas las huellas idénticas | entrada de rendimiento; D51 |
| Evaluar los 18 cuadros aislados | reloj exacto implementado: 0 cuadros distintos, también en modo estricto | entrada del reloj; D47 |

## Comprobaciones de los hitos anteriores, con el código final

| Comprobación | Resultado |
|---|---|
| `make test` (ctest) | 8 de 8 (también en `build-asan`) |
| `make check` (paso cerrado 600 cuadros contra Musashi, hito 1) | OK |
| `make check-blind` (paso cerrado 12.106 cuadros con la secuencia de validación) | OK: sin divergencia, memoria idéntica |
| `make analyze` | 43.036 instrucciones, 1.339 funciones, 0 conflictos |
| `make dasmcheck` | 43.036 instrucciones, 0 discrepancias de longitud con el desensamblador de Musashi |
| `make compare-mame` (hito 2) | 12.000 de 12.000 cuadros iguales; 0 con diferencias de registros o pila |
| `make compare-video` (hito 3) | 17 de 17 cuadros idénticos a MAME |
| `make compare-sound-cmds` (hito 4) | las 1.391 escrituras coinciden en latch, valor y cuadro |
| `make compare-audio` (hito 4) | todo igual: accesos del Z80, chips, mezcla y WAV (7 tramos exactos) |
| Web (`make web`, Emscripten local) | compila; pruebas wasm en Node 3 de 3, estado y vídeo de la partida de 12.000 cuadros idénticos a macOS, audio con 985 muestras a ≤ 1 LSB y D33 pasa (entrada de plataformas) |
| Linux arm64 y x86-64 (`tools/linux-docker.sh`) | `ctest` 8 de 8 en las dos tras arreglar el enlace de `libsf2py.so` (`-fPIC`, entrada de plataformas); estado, vídeo y resumen idénticos a macOS; audio bit a bit en arm64 y con D33 en x86-64; las 13 secuencias reproducen su CRC de estado |
| Windows (`build-win`, llvm-mingw) | compila tras un arreglo (abajo); con Wine (`tools/windows-wine-docker.sh`), 633 y 7.898 comprobaciones sin fallos, estado y vídeo idénticos a macOS, audio igual al de Linux x86-64 (D33 pasa) |

**Arreglo del build cruzado**: el sub-proyecto que genera el C en la máquina anfitriona (`sf2_hostgen`, `cmake/SF2Generation.cmake`)
tenía `BUILD_ALWAYS 0`, así que `cmake --build build-win` no volvía a entrar en él y compilaba el C de un recompilador anterior
(sin `recomp_clock_mame`: fallo de enlace). Ahora `BUILD_ALWAYS 1`: se entra siempre en el sub-build, que es incremental y solo
regenera si cambian las ROMs o el recompilador. La web no lo sufrió porque `make web` reconfigura.

**Arreglo del build de Linux** (entrada `2026-10-20-hito6-plataformas`): `libsf2py.so` (objetivo `sf2py`, en `all`) enlaza las
bibliotecas estáticas del núcleo, que el clang de Debian compila PIE, y `ld` lo rechazaba («recompile with -fPIC»): fallaba el
build entero de Linux. Ahora, fuera de Apple y Windows, esas bibliotecas se compilan con `POSITION_INDEPENDENT_CODE ON`
(`tools/CMakeLists.txt`). En macOS no cambia ni una opción de compilación (`compile_commands.json` idéntico) y en Linux da las
mismas huellas a la misma velocidad.

## Archivos del hito

- Nuevos: `tools/playgen.py`, `tools/sf2py.c` (biblioteca `libsf2py`), `tools/coverage.py`, `tools/mame_m68000_ack.py`,
  `tools/sequences/` (14 secuencias y `README.md`), `recompiler/m68000_ack.txt`, `tests/game_dispatch_failure.cmake`,
  `third_party/musashi/PARCHES.md`.
- Cambiados: `recompiler/gen_c.py` (reloj de MAME, códigos `ke`, `inline_text`), `tools/analyze.py` y `tools/hints.txt` (D49),
  `tools/mame_blind.lua` (dipswitches, comandos de sonido, traza), `runtime/` (`cpu.h`, `machine.c/.h`, `sf2.c/.h`, `video.c`,
  `gfx.c/.h`, `sf2_port.h`), `harness/` (despacho, `--dips`, `--fallback`, `--dispatch-log`, `--roundtrip`,
  `--trace-cycles`), `app/main.c` (fallos de despacho, rescate, `--debug-jump`), `tests/`, `CMakeLists.txt` (`SF2_SANITIZE`),
  `cmake/` (dependencia de la tabla del microcódigo, `BUILD_ALWAYS`), `tools/CMakeLists.txt` (`sf2py`, `-fPIC` fuera de Apple
  y Windows), `Makefile` (`coverage`, `compare-coverage`), Musashi
  (`m68k_in.c`, 8 desplazamientos), los `README.md` de cada carpeta, `docs/PLAN.md`, `docs/DECISIONES.md` (D46-D52).

## Lo que queda abierto

En `docs/PLAN.md`, «Estado del proyecto al cerrar el hito 6». Lo principal: el 26 % del código que ninguna partida ejecuta (sin
conflictos en el análisis y con el fallo a la vista si llegara), probar fuera de macOS en máquinas reales, la capa de estrellas
y el volteo de pantalla, y las extensiones (netplay con rollback, mods).

## Comandos

```bash
make && make test
make check check-blind analyze dasmcheck
make compare-mame compare-video compare-sound-cmds compare-audio
make coverage                      # build/coverage/informe_cobertura.md
make compare-coverage              # build/coverage/informe_mame.md (unos 20 minutos)
python3 tools/coverage.py --roundtrip --jobs 3 --roms sf2      # build/coverage/informe_estados.md
python3 tools/playgen.py --verify tools/sequences/*.txt
tools/linux-docker.sh && PLATFORM=linux/amd64 tools/linux-docker.sh    # ver la entrada de plataformas
tools/windows-wine-docker.sh
```
