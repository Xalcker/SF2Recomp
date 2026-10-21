# 2026-10-19 · Hito 6 · Sanitizadores, ejecución larga y estados a mitad de partida

## Pregunta de partida

¿Hay errores de memoria o comportamiento indefinido en el núcleo, el juego o los arneses cuando corren las partidas completas
(no solo los 12.000 cuadros de validación del hito 5)? ¿Crece la memoria en una sesión larga? ¿Un estado guardado a mitad de una
partida la reproduce exactamente al cargarlo?

## Qué se hizo

### Sanitizadores (AddressSanitizer + UndefinedBehaviorSanitizer, macOS arm64, Apple clang 21)

- Opción nueva de CMake `SF2_SANITIZE` (p. ej. `address,undefined`) que compila todo el proyecto con
  `-fsanitize=… -fno-omit-frame-pointer -fno-sanitize-recover=all`: cualquier aviso detiene el proceso, así que `ctest` y
  `tools/coverage.py` lo ven como un fallo y no como texto en la salida. Árbol aparte: `build-asan` (Release, ROMs de `sf2/`).
- Lo que corrió con sanitizadores, con el código final del hito:
  1. `ctest` (las 8 pruebas: unitarias, ROMs y estados, paso cerrado con Musashi, resumen con y sin cesión, pureza del núcleo,
     juego sin ventana, fallo de despacho provocado, mandos virtuales).
  2. `tools/coverage.py --blind build-asan/sf2_blind` (las 14 secuencias de `tools/sequences/`, 603.050 cuadros, sin red de
     seguridad, con cobertura, registro de despachos y comandos de sonido): las 14 terminan, misma cobertura que el build
     normal (31.774 instrucciones).
  3. Las 14 secuencias otra vez con vídeo, sonido y PNG (`--gfx --wav --png-frames`) y una tercera con `--roundtrip` (abajo):
     resumen por cuadro idéntico al del build normal en las 28 ejecuciones; los WAV comprobados (validación, servicio,
     retador) idénticos bit a bit al del build normal.
  4. El juego (`build-asan/sf2`, SDL con los controladores `dummy`, sonido y ventana) 110.000 cuadros de la partida de Guile y
     la atracción (30 min 44 s emulados, 202 s).
- Resultado: **`ctest` 8 de 8, las 42 ejecuciones de secuencias y el juego terminan sin ningún aviso**.
- **Encontrado**: un único aviso, y no en nuestro código: UBSan detenía `lockstep_vs_musashi` en `m68kops.c` por `1 << 31` sobre
  `int` en BTST/BCHG/BCLR/BSET de 32 bits de Musashi (comportamiento indefinido en C; en la práctica daba lo mismo). Parche
  local de 8 sitios (`1u << …`), anotado en `third_party/musashi/PARCHES.md`. Musashi solo está en los arneses (D13).
- Nada en el núcleo, el renderizador (también el nuevo de la entrada de rendimiento), el sonido, la aplicación ni los
  arneses.

### Ejecución larga sin ventana

`build/sf2 sf2.zip --nothrottle --frames N --inputs tools/sequences/guile.txt` con SDL `dummy` (vídeo, sonido y ventana como
en el juego): la partida completa de Guile (70.817 cuadros) y después la atracción en bucle.

| Ejecución | Cuadros (tiempo emulado) | Duración | Memoria |
|---|---|---:|---|
| 1 (binario antes de optimizar) | 110.000 (30 min 44 s) | 104 s | RSS cada 2 s: 34-57 MB, sin tendencia (baja al final) |
| 2 (ídem) | 110.000 | ~110 s | huella física (`vmmap`) cada 10 s: 36 → 23 MB y plana; `leaks --atExit`: 0 fugas, pero el proceso no era depurable y `leaks` avisó de que solo veía memoria de solo lectura |
| 3 (binario final, copia firmada con `get-task-allow`) | **220.000 (61 min 29 s)** | 179 s | **huella física 24,5 MB en todas las muestras de principio a fin**; RSS 24-37 MB; `leaks --atExit` con el montón visible: **3.336 bloques vivos (524 KB) y 0 fugas** |

La memoria del juego es fija desde el arranque (estados de rescate en un búfer reutilizado, D46; tablas del renderizador una
vez en `gfx_init`); lo que se mueve en el RSS son páginas del sistema (SDL, audio) que el sistema recupera.

### Guardar y cargar a mitad de partida (`tools/coverage.py --roundtrip`)

`sf2_blind --roundtrip a,b,c…` (cede en cada VBlank): en cada cuadro de la lista guarda el estado completo, reinicia la máquina,
llena la RAM de trabajo de 0xA5 y la gráfica de 0x5A, carga el estado y comprueba que volver a guardarlo da los mismos bytes. Los
puntos son 4 repartidos por la partida más uno 10 cuadros después de cada hito anotado (selección, cada asalto, bonus, final,
CONTINUE?, menú…). Cada secuencia corre dos veces con sonido, seguida y con los puntos, y se comparan el resumen por cuadro y el WAV
de 48 kHz.

| Secuencia | Puntos | Resumen | WAV |
|---|---:|---|---|
| blanka | 59 | idéntico | idéntico (105 MB) |
| chunli | 60 | idéntico | idéntico (110 MB) |
| continuar | 26 | idéntico | idéntico |
| dhalsim | 59 | idéntico | idéntico |
| ehonda | 59 | idéntico | idéntico |
| guile | 59 | idéntico | idéntico (114 MB) |
| ken | 59 | idéntico | idéntico |
| retador | 19 | idéntico | idéntico |
| ryu | 59 | idéntico | idéntico |
| servicio | 15 | idéntico | idéntico |
| sin_continuar | 18 | idéntico | idéntico |
| validacion | 15 | idéntico | idéntico |
| versus | 20 | idéntico | idéntico |
| zangief | 59 | idéntico | idéntico |

**586 puntos de guardar, reiniciar y cargar; 14 de 14 secuencias con resumen y WAV idénticos.** El estado (versión 2, D47)
lleva todo lo que la máquina necesita, también el punto de muestreo de la IRQ y el estado del Z80, el YM2151 y el OKI. Informe:
`build/coverage/informe_estados.md`.

## Comandos

```bash
cmake -S . -B build-asan -DSF2_SANITIZE=address,undefined -DSF2_ROM_DIR=$PWD/sf2 && cmake --build build-asan -j8
ctest --test-dir build-asan --output-on-failure
python3 tools/coverage.py --blind build-asan/sf2_blind --rom build-asan/rom.bin --out build/coverage_asan
python3 tools/coverage.py --roundtrip --jobs 3 --roms sf2          # build/coverage/informe_estados.md
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/sf2 sf2.zip --quiet --nothrottle --inputs tools/sequences/guile.txt --frames 220000
leaks --atExit -- copia_firmada_de_sf2 sf2.zip --quiet --nothrottle --inputs tools/sequences/guile.txt --frames 220000
```

(`leaks` necesita un ejecutable con el derecho `com.apple.security.get-task-allow`: se firmó una copia en un directorio temporal
con `codesign -s - -f --entitlements`, sin tocar el binario del árbol de compilación.)

## Pendiente

- LeakSanitizer no existe en macOS arm64; las fugas se miden con `leaks`. En Linux (Docker) se podría pasar `ctest` con LSan.
- Los sanitizadores no se han pasado en Windows ni en wasm.
