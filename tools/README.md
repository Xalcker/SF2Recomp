# tools/

## probe_rom.py

Intercala los ocho ROMs de programa de la revisión World 910522 (cuatro
pares de bytes pares e impares) en una imagen plana de 0x100000 bytes, imprime los vectores de reset e interrupción, un conteo
aproximado de instrucciones de salto (barrido lineal, con falsos positivos por
datos), estadísticas por segmento de 256 KB y el SHA-1 de la imagen resultante.

```bash
python3 -I tools/probe_rom.py sf2 /ruta/salida/sf2_prog.bin
```

La imagen de salida contiene código de Capcom: no la guardes dentro del
repositorio.

## mame.sh

Lanza el MAME mínimo compilado en `external/mame/cps1` con las ROMs de la raíz
del proyecto (`sf2.zip`) y los datos de MAME en `external/mame-data/`.

```bash
tools/mame.sh -verifyroms sf2
```

```bash
tools/mame.sh sf2
```

## mame_trace.lua

Activa la traza del 68000 en una ejecución sin ventana. `-debugscript` no
funciona con `-debugger none`, por eso se hace desde Lua.

```bash
SF2_TRACE=/tmp/traza.log tools/mame.sh sf2 -video none -sound none -nothrottle -str 5 -debug -debugger none -autoboot_script tools/mame_trace.lua
```

## mame_blind.lua (hito 2)

Produce desde MAME el mismo resumen por cuadro que `build/sf2_blind` e inyecta la misma secuencia
de entradas con `manager.machine.ioport`. No necesita `-debug`: detecta cada ejecución del
manejador de IRQ2 con un tap de lectura sobre 0x800172 (`install_read_tap`) filtrado por
`CURPC == 0xAD0`, y en ese instante lee registros (`cpu.state`), la RAM (`read_range`) y los
puertos. Variables de entorno: `SF2_SUMMARY` (salida), `SF2_INPUTS`, `SF2_FRAMES` (termina con
`machine:exit()` al llegar), `SF2_DETAIL=1`, `SF2_DUMP_FRAME`/`SF2_DUMP_DIR`, `SF2_SNAP_FRAMES`
(capturas PNG en el directorio de capturas de MAME, `cuadro_N.png`).

```bash
SF2_SUMMARY=build/summary_mame.txt SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
  tools/mame.sh sf2 -video none -sound none -nothrottle \
    -keyboardprovider none -mouseprovider none -joystickprovider none -lightgunprovider none \
    -str 240 -autoboot_script tools/mame_blind.lua
```

Los `-*provider none` son obligatorios para una ejecución reproducible: aunque no haya ventana, el
OSD de MAME registra el teclado, el ratón y el puntero del sistema (`-verbose`: "Input: Adding
keyboard #1: System keyboard") y en macOS el proceso recién lanzado recibe el foco, así que una
tecla pulsada mientras corre llega al juego (una flecha izquierda se leyó como P1 Left en una
verificación). Con los proveedores a `none` no se registra ningún dispositivo y las únicas entradas
son las de la tabla. `make compare-mame` ya los pasa (`MAME_HEADLESS`).

Detalles que cuestan de descubrir: los taps hay que guardarlos en una variable global (si el objeto
se recoge como basura, el tap desaparece); los taps no ven las búsquedas de opcode, solo accesos a
datos; `read_range` con anchura 16 devuelve palabras en el orden de bytes del anfitrión (se usa
anchura 8); `screen.frame_number` es un método en 0.289; `emu.register_stop` no existe (es
`emu.add_machine_stop_notifier`); un `set_value` hecho en el cuadro N se lee en N+1.

## compare_summaries.py (hito 2)

Compara los dos resúmenes y señala el primer cuadro distinto:

```bash
python3 tools/compare_summaries.py build/summary_recomp.txt build/summary_mame.txt [--context N] [--all]
python3 tools/compare_summaries.py --ram build/rec/wram_1062.bin build/mame/wram_1062.bin   # volcados
```

`DIFERENCIA` si cambian `w` (RAM de trabajo sin zonas volátiles), `g` (RAM gráfica), `in` (entradas
leídas) o `fc` (contador de cuadros del juego); las diferencias de PC/registros/pila (`v`) son solo
información (la IRQ cayó en otro punto del bucle de espera). Imprime las rachas de cuadros
distintos, si vuelven a coincidir, el histograma de pares de PC interrumpido y, con `--detail` en
los resúmenes, los bloques de RAM que difieren. Código de salida 0 si coinciden o si solo hay
rachas aisladas (≤ `--max-run`, 3) seguidas de resincronización; 1 si hay una racha más larga, no
resincroniza, o **el campo de entradas difiere en algún cuadro**: con `--inputs tabla` dice si en
ese cuadro había inyección programada (desalineación) o no (entrada ajena a la tabla, como la del
teclado del anfitrión descrita arriba). `--strict` exige igualdad total.

## mame_snap.lua (hito 3)

Capturas de MAME para la comparación píxel a píxel, con la misma secuencia de entradas y la misma
numeración de cuadros que `mame_blind.lua` (tap sobre 0x800172 con `CURPC == 0xAD0`). En cada cuadro de
`SF2_SNAP_FRAMES` llama a `screen:snapshot("cuadro_N.png")`, que devuelve el cuadro que `vblank_begin`
renderizó justo antes de la IRQ2; con `SF2_DUMP_DIR` vuelca además `gfx_N.bin` (RAM gráfica),
`regs_N.bin` (CPS-A y CPS-B, 0x40 + 0x40 bytes big-endian, leídos en `emu.register_frame_done`: los que
dibujaron el cuadro, antes de que el manejador toque la base OBJ/OTHER), `obj_N.bin` (la tabla de objetos
usada por el cuadro, la leída en el `frame_done` anterior) y `pal_N.bin` (los 0xC00 pens de MAME como
0x00RRGGBB, `screen.palette:pen_color`). `make mame-video` lo lanza con `MAME_HEADLESS` y
`-snapshot_directory build/video/mame` (prevalece sobre el de `mame.sh`).

```bash
SF2_SNAP_FRAMES=100,650,1600 SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=1601 SF2_DUMP_DIR=$PWD/build/video/mame \
  tools/mame.sh sf2 -video none -sound none -nothrottle \
    -keyboardprovider none -mouseprovider none -joystickprovider none -lightgunprovider none \
    -snapshot_directory $PWD/build/video/mame -str 60 -autoboot_script tools/mame_snap.lua
```

## compare_frames.py (hito 3)

Compara PNG a PNG sin dependencias (lee y escribe PNG con `zlib`; 8 bits RGB/RGBA, filtros 0-4):

```bash
python3 -I tools/compare_frames.py A.png B.png [--diff diff.png]
python3 -I tools/compare_frames.py --dir-a build/video --dir-b build/video/mame --frames 100,650,1600 --diff-dir build/video/diff
```

Por cuadro: píxeles distintos, rectángulo que los contiene, primeros píxeles con el color de cada lado y
`diff_N.png` (captura atenuada con los píxeles distintos en rojo, en amarillo si solo cambia el brillo).
Código 0 si todo coincide (o dentro de `--max-bad N` píxeles por cuadro), 1 si hay diferencias, 2 si falta
un archivo. `make compare-video` lo ejecuta sobre `VFRAMES`.

## render_dump.c (hito 3)

`build/render_dump gfx_N.bin regs_N.bin obj_N.bin pal_N.bin salida.png [--gfx sf2] [--pal-runtime]`
renderiza un cuadro con `runtime/video.c` a partir de los volcados de `mame_snap.lua`, sin ejecutar el
juego: si la salida coincide con `cuadro_N.png` de MAME el renderizador es correcto y una diferencia del
runtime completo viene del estado o de la temporización. `--pal-runtime` reconstruye la paleta con
`mem_palette_copy` en lugar de usar los pens de MAME.

## mame_audio.lua (hito 4)

Referencia de sonido desde MAME con la misma partida y numeración de cuadros que `mame_blind.lua`. Escribe
en `SF2_AUDIO_DIR` (debe existir):

| Archivo | Contenido |
|---|---|
| `events.txt` | `F n t68` (punto de muestra del cuadro n), `L k v t68 tz` (escritura del 68000 en el latch k), `W a v tz` / `R a v tz` (escritura / lectura del Z80 en 0xF000-0xF00F con el valor); t68 en ciclos del 68000, tz del Z80, desde el arranque |
| `ym.f32` | salida del YM2151, float32 L R intercalados, 55.930 Hz |
| `oki.f32` | salida del OKI, float32, 7.575 Hz |
| `mono.f32` | entrada del altavoz, float32, 48 kHz (lo que graba `-wavwrite`) |
| `chunks.txt` | muestras de cada actualización periódica: `ym oki mono` |

```bash
SF2_AUDIO_DIR=$PWD/build/audio/mame SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
  tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
    -joystickprovider none -lightgunprovider none -str 240 -wavwrite $PWD/build/audio/mame/sf2_mame.wav \
    -autoboot_script tools/mame_audio.lua
```

`-wavwrite` (opción de MAME, no del script) deja en la misma pasada el WAV de la mezcla del altavoz
(`sf2_mame.wav`, 16 bits, 48 kHz, mono) para la comparación de WAV de `compare_audio.py`.
`make mame-audio` hace exactamente esto con `MAME_HEADLESS`.

Cómo funciona: taps de escritura en 0x800180-0x80018F (68000) y de lectura/escritura en 0xF000-0xF00F del
espacio de `:audiocpu`; dentro de un tap `manager.machine.time` es el tiempo local de la CPU que accede, el
comienzo del ciclo M del acceso en el núcleo Z80 de 0.289, y se redondea al ciclo más próximo (MAME suma
attosegundos por ciclo truncados). Los flujos llegan por `emu.register_sound_update` tras poner
`hook = true` en `manager.machine.sounds[":2151"]`, `[":oki"]` y `[":mono"]`: cada llamada trae las muestras
generadas desde la anterior (actualización periódica de 50 Hz). Con `-sound none` funciona igual.

## mame_sound_cmds.lua y compare_sound_cmds.py (hito 4)

Primer nivel de validación del sonido: los comandos que el 68000 manda al Z80, sin ROMs de sonido.

```bash
SF2_CMDS=$PWD/build/audio/cmds_mame.txt SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
  tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
    -joystickprovider none -lightgunprovider none -str 240 -autoboot_script tools/mame_sound_cmds.lua
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt --sound-cmds build/audio/cmds_recomp.txt --quiet
python3 tools/compare_sound_cmds.py build/audio/cmds_recomp.txt build/audio/cmds_mame.txt
```

Los dos escriben una línea por escritura en un latch, `cuadro latch valor` (latch 1 = 0x800181, comando; latch 2 =
0x800189, fade; valor en hexadecimal), y al final `# cuadros N`; el cuadro es el número de puntos de muestra
(0xAD0 del manejador de la IRQ2) pasados, como en `mame_blind.lua`. El comparador mira los cuadros anteriores al
último común, imprime un resumen del flujo por tramos de la partida y, si algo difiere, la primera escritura
distinta con tres a cada lado de contexto. Código 0 si coinciden, 1 si no. `make compare-sound-cmds` lo hace todo.

## compare_audio.py (hito 4)

```bash
python3 tools/compare_audio.py build/audio/recomp build/audio/mame [--sections] [--report informe.md] [--context N]
python3 tools/compare_audio.py runtime.wav mame.wav [--report informe.md]    # solo los dos WAV
```

Compara dos directorios con el formato anterior (el runtime los escribe con `sf2_blind --audio-dir`) por
capas: cuadros y comandos del 68000 (valor, latch y cuadro; el instante es informativo), accesos del Z80
(dirección, valor y ciclo, con el contexto de la primera diferencia), los tres flujos bit a bit (primera
muestra distinta, diferencia máxima, señal/ruido; `--sections` añade el resultado por tramos de la partida
de `inputs_sf2.txt`) y, si están `sf2.wav` (runtime) y `sf2_mame.wav` (MAME), los dos WAV por tramos con
criterios de energía y espectro que no exigen igualdad muestra a muestra:

| Criterio | Por tramo | Por defecto |
|---|---|---|
| energía | diferencia del RMS en dBFS (un tramo por debajo de −80 dBFS es silencio) | ≤ 0,5 dB (`--tol-energy`) |
| bandas | diferencia de la energía en cada una de las 9 bandas de octava (0-100 Hz a 12,8-24 kHz) a menos de 60 dB de la más fuerte | ≤ 1,0 dB (`--tol-band`) |
| espectro | distancia espectral logarítmica por bin (2048 puntos, Hann, hasta `--windows` 32 ventanas con señal) | ≤ 1,0 dB (`--tol-lsd`) |
| exactitud | muestras distintas, diferencia máxima en LSB, señal/ruido | solo informativo |

`--report archivo` escribe un informe en Markdown (tabla por tramo, bandas de octava y la salida completa;
`make compare-audio` lo deja en `build/audio/informe_audio.md`). Solo compara hasta el último punto de muestra
común. Código 0 si todo coincide y los tramos pasan, 1 si no. Sin dependencias (FFT en Python, ~4 s para los
203 s). `make compare-audio` lo ejecuta tras `run-audio` y `mame-audio`.

## z80dis.c (hito 4)

`build/z80dis sf2/sf2_09.bin [desde [hasta]]` desensambla el programa de sonido con `z80_disasm` de
`runtime/z80.c` (direcciones en hexadecimal; en 0x8000-0xBFFF muestra el banco 0 como el archivo).

## inputs_sf2.txt (hito 2)

Secuencia de entradas "a ciegas": `desde hasta tecla` por línea (cuadros = entregas de IRQ2,
inclusive). Teclas: `coin1 coin2 service start1 start2 test`, `p1_up p1_down p1_left p1_right
p1_lp p1_mp p1_hp p1_lk p1_mk p1_hk` y las `p2_*`. La actual: moneda en el 600, start en el 1000
(título), confirmación de Ryu en el 1200, y un patrón de avance y golpes durante el combate contra
Blanka hasta el 5500; después el juego sigue solo (continuar, fin de partida, atract).

## interleave.py (hito 1)

Paso de construcción que intercala los cuatro pares de ROMs de programa en `build/rom.bin`
(0x100000 bytes); lo ejecuta CMake como primer paso del recompilador (`cmake/SF2Pipeline.cmake`). Desde el hito 5
identifica las ROMs por **CRC-32 y tamaño** (la misma tabla que `runtime/romset.c`) y acepta una carpeta con los
archivos sueltos o el `sf2.zip` de MAME, con los nombres antiguos o los actuales; si falta alguna dice cuál, con su
CRC. `probe_rom.py` queda como herramienta de inspección.

```bash
python3 tools/interleave.py sf2 build/rom.bin        # carpeta
python3 tools/interleave.py sf2.zip build/rom.bin    # zip de MAME
```

## linux-docker.sh, windows-wine-docker.sh (hito 5)

Compilan y prueban en Linux (arm64 o x86-64, `PLATFORM=linux/amd64`) dentro de Docker, y prueban con Wine los `.exe` de
Windows compilados de forma cruzada, sin instalar nada en la máquina anfitriona; ver `README.md` de la raíz.
`docker/linux.Dockerfile` y `docker/wine.Dockerfile` son sus imágenes (herramientas; no llevan código ni ROMs).

## m68k.py (hito 1)

Decodificador completo del MC68000 (sin 68010/020), usado por `analyze.py` y por
`recompiler/gen_c.py`. Como programa, desensambla linealmente desde una dirección:

```bash
python3 tools/m68k.py build/rom.bin 40e 40
```

## analyze.py (hito 1)

Desensamblado por descenso recursivo desde la tabla de vectores, resolución de las tablas de
salto de Capcom y partición en funciones. Escribe el informe de cobertura, el desensamblado
completo con etiquetas y la lista `dirección longitud texto` para `dasmcheck`:

```bash
make analyze     # build/coverage.txt, build/disasm.txt, build/insns.txt
```

## hints.txt (hito 1)

Correcciones manuales del análisis, una por línea: `entry DIR` (punto de entrada), `label DIR`
(inicio de bloque despachable), `table_end BASE FIN` (acota una tabla de salto). Vacío por ahora:
ni el arnés en paso cerrado ni el arnés a ciegas han necesitado ninguna en toda la partida a ciegas.

## dump_cycles.c y dasmcheck.c (hito 1)

`dump_cycles` vuelca la tabla de ciclos base del 68000 de Musashi (`build/cycles68000.bin`) para
la temporización del código generado. `dasmcheck` compara la longitud de cada instrucción
decodificada por `m68k.py` con la del desensamblador de Musashi (`make dasmcheck`).

## Hito 6: secuencias, cobertura y reloj exacto

### sequences/ y playgen.py

`tools/sequences/` guarda las 14 partidas que recorren el juego (formato y lista en `tools/sequences/README.md`).
`playgen.py` las genera jugando sobre el núcleo recompilado (`build/libsf2py.dylib`, objetivo `sf2py`; ctypes):

```bash
python3 tools/playgen.py --char ryu --out tools/sequences/ryu.txt                      # partida completa
python3 tools/playgen.py --mode versus|challenger|service ...                         # dos jugadores, retador, menú de pruebas
python3 tools/playgen.py --char guile --lose-after 2 --continues 1 --out ...          # continuar y fin de partida
python3 tools/playgen.py --verify tools/sequences/*.txt                                # reproduce, comprueba #@ state_crc, anota hitos
python3 tools/playgen.py --resume salida.txt.partial --char ... --out salida.txt      # sigue una partida interrumpida
```

### hitbox_probe.py y sf2py (mods)

`hitbox_probe.py` lee las cajas de golpe y de daño de una secuencia en un cuadro con la especificación de `docs/HITBOXES.md` y las dibuja sobre la captura (`python3 -I tools/hitbox_probe.py --seq tools/sequences/ryu.txt --frame 1708 --out caja.png [--list] [--zoom 2]`). `sf2py` (biblioteca para ctypes) expone además `sf2py_training` y `sf2py_training_history` para probar el mod de entrenamiento (`docs/ENTRENAMIENTO.md`) desde Python.

### coverage.py

`make coverage` corre todas las secuencias a ciegas en `sf2_blind` **sin red de seguridad de Musashi** y escribe
`build/coverage/informe_cobertura.md`: instrucciones y funciones ejecutadas frente a las estáticas, despachos por la
tabla clasificados (y los destinos que el análisis no había previsto para esa instrucción de origen), fallos de despacho,
conflictos del análisis y los tramos más largos de código no ejecutado. `make compare-coverage` (o `coverage.py --mame`) valida además cada secuencia contra MAME:
resumen por cuadro, comandos de sonido y vídeo en los cuadros de `#@ snap` y uno cada 2.000
(`build/coverage/informe_mame.md`). `coverage.py --roundtrip` corre cada secuencia seguida y guardando, reiniciando y
cargando el estado en varios puntos, con sonido: resumen y WAV tienen que salir idénticos (`informe_estados.md`).
`--jobs N`, `--only nombre...`, `--report-only` (rehace los informes con lo que hay en `build/coverage/`; con
`--mame-report` también el de MAME, para juntar lotes corridos con `--only`), `--blind`/`--rom`/`--out` (otro build, p. ej.
el de sanitizadores: `--blind build-asan/sf2_blind --rom build-asan/rom.bin --out build/coverage_asan`).

### mame_blind.lua (ampliado)

`SF2_DIPS=aa,bb,cc` fija los dipswitches (con `-cfg_directory` propio; ver D50), `SF2_CMDS=archivo` anota los comandos de
sonido como `mame_sound_cmds.lua` en la misma pasada, y `SF2_TRACE=archivo SF2_TRACE_FRAMES=desde:hasta` (con `-debug
-debugger none`) escribe la traza del 68000 con el ciclo total de cada instrucción (`0:N` desde el reset). Con
`sf2_blind --trace-cycles desde:hasta archivo` se compara instrucción a instrucción (evaluación del reloj, D47).

### mame_m68000_ack.py

Lee el microcódigo generado del 68000 de MAME (`external/mame/src/devices/cpu/m68000/m68000-sdf.cpp` y `-decode.cpp`) y
escribe `recompiler/m68000_ack.txt`: por entrada de la tabla de decodificación, los caminos de cada instrucción con su
total de ciclos y el punto en que se decide si se atiende una interrupción (D47). Solo hace falta volver a ejecutarlo si
cambia la versión de MAME de referencia.

```bash
python3 tools/mame_m68000_ack.py external/mame/src/devices/cpu/m68000/m68000-sdf.cpp recompiler/m68000_ack.txt
```
