# tests/

`ctest --test-dir build --output-on-failure` (o `make test`). Sin ROMs corren `unit`, `core_purity` e `input_mapping`; el
resto necesita `SF2_ROM_DIR`.

| Prueba | Qué comprueba |
|---|---|
| `unit` (`unit.c`) | CRC-32, inflate (tres vectores de zlib, cortados, 2.000 entradas basura), zip en memoria, catálogo de ROMs y su informe, flujo de estados, configuración (ida y vuelta, dipswitches de MAME, valores no válidos), orden de bytes del Z80 y tabla de pasos del OKI contra `pow` |
| `roms_and_states` (`rom_tests.c`) | carga desde zip y carpeta con las mismas ROMs, CRC de la imagen contra el código generado, informe con una ROM menos, estados: otra máquina retoma 360 cuadros idénticos (vídeo y audio), rebobinar, estados encadenados, 7 formas de estado dañado rechazadas sin tocar la máquina, guardado en cualquier punto de 4.000 cuadros; hito 6: un salto a una dirección sin código detiene la máquina con el mensaje y el `entry` que falta, y cargar un estado la recupera |
| `lockstep_vs_musashi` | `make check`: 600 cuadros del código recompilado contra Musashi sin divergencia |
| `summary_run_vs_step` | el resumen de 3.000 cuadros es idéntico ejecutando de seguido y cediendo tras cada VBlank (D39) |
| `core_purity` (`check_core_purity.cmake`) | ninguna llamada al sistema en las bibliotecas del núcleo (D37) |
| `game_frames_headless` (`game_frames.cmake`) | el juego `sf2` sin ventana (controlador `dummy` de SDL), leyendo de `sf2.zip`, escribe los mismos PNG que `sf2_blind`, que son los de MAME |
| `game_dispatch_failure` (`game_dispatch_failure.cmake`) | hito 6 (D46): el juego sin ventana, con `--debug-jump`, se detiene con error, lo dice por stderr, deja `errores.log` con destino, origen y remedio y un estado de rescate que con `--load-state` vuelve a correr |
| `input_mapping` (`input_test.c`) | remapeo con mandos virtuales de SDL3 (los mandos reales del equipo se desactivan): nombres por omisión válidos, jugador 1 y 2, cruceta, stick, gatillos, asignación cambiada |

Otras herramientas de este directorio: `trace.c` (`sf2_trace`, huellas de una partida para comparar plataformas, D45),
`gen_vectors.py` (genera `vectors.h`). En WebAssembly `unit`, `roms_and_states` y `core_purity` corren en Node.js
(`ctest --test-dir build-web`); los `.exe` de Windows no se pueden ejecutar aquí (ver `tools/windows-wine-docker.sh`).

Sanitizadores (hito 6): `cmake -S . -B build-asan -DSF2_ROM_DIR=... -DSF2_SANITIZE=address,undefined` compila todo (código
generado incluido) con AddressSanitizer y UndefinedBehaviorSanitizer; un aviso detiene el proceso
(`-fno-sanitize-recover=all`), así que `ctest --test-dir build-asan` y `tools/coverage.py --blind build-asan/sf2_blind` fallan
con cualquier aviso. Resultados en la bitácora del hito 6.

