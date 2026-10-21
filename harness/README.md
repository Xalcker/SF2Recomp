# harness/

Arneses de validación de los hitos 1 a 4. Musashi (`third_party/musashi`, MIT) solo se usa aquí y en las herramientas;
el juego (`app/`) no depende de él (D13). Desde el hito 5 la máquina CPS1 vive en el núcleo (`runtime/machine.c`) y el
visor SDL3 `sf2_video` se sustituyó por el juego `sf2` (`app/`); aquí queda lo que añaden los arneses.

## Binarios

```bash
make -j8                                  # o: cmake --build build -j8
build/sf2_lockstep build/rom.bin --frames 600                      # recompilado vs Musashi (= make check)
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt \
    --summary build/summary_recomp.txt --io-table build/io_table.md   # máquina a ciegas (= make run-blind)
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt --step \
    --summary build/summary_step.txt                                  # igual, cediendo el control tras cada VBlank (D39)
build/sf2_blind build/rom.bin --frames 1601 --inputs tools/inputs_sf2.txt \
    --png-frames 100,650,1600 --png-dir build/video --gfx sf2.zip     # PNG sin ventana (= make png-video)
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt \
    --audio-dir build/audio/recomp --wav build/audio/recomp/sf2.wav   # sonido sin ventana (= make run-audio)
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt \
    --wav build/audio/sf2_recomp.wav                                  # solo el WAV (= make wav-recomp)
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt \
    --sound-cmds build/audio/cmds_recomp.txt                          # solo los comandos de sonido (= make compare-sound-cmds)
build/sf2 sf2.zip --inputs tools/inputs_sf2.txt                       # el juego con la misma partida (= make run-video)
```

`--gfx`, `--sound-roms` y `--roms` (los dos a la vez) aceptan una carpeta con las ROMs sueltas o el `sf2.zip` (D38).
`rom.bin` es la imagen de programa de 1 MiB que genera el build (`tools/interleave.py`).

### `harness.c` / `harness.h` (hito 5): lo que añaden los arneses a la máquina del núcleo

`harness_install(mc)` conecta a `runtime/machine.c` por ganchos la cobertura dinámica y el anillo de las 64 últimas
direcciones (`on_insn`) y la red de seguridad con Musashi (`on_unknown`); `harness_run(mc, on_finish)` ejecuta con
`machine_run`, imprime el informe (cuadros, instrucciones, episodios de Musashi, E/S, cobertura, tabla de E/S, sonido) y
termina el proceso con 0, o con 2 y los registros y el anillo de traza si el código recompilado falló. Es lo que hacía el
antiguo `harness/machine.c`; el planificador de la IRQ2 y los ganchos `on_vblank`/`on_sample` pasaron a
`runtime/machine.c` (ver `runtime/README.md`).

Red de seguridad (solo con `harness_install(mc, 1)`, opción `--fallback` de `sf2_blind`; D46): si el control llega a una
dirección sin función recompilada, el estado de la CPU pasa a Musashi, que interpreta sobre la misma memoria hasta que el PC
vuelve a una dirección de la tabla (las IRQ las atiende siempre el manejador recompilado); el episodio se registra (entrada,
salida, instrucciones) para añadir `entry` a `tools/hints.txt`. Por omisión (hito 6) no hay red: la máquina se detiene con el
mismo mensaje que el juego («salto a XXXXXX desde la instrucción YYYYYY…») y `sf2_blind` sale con 2.

Hito 6: `harness_set_dispatch_log(mc, archivo)` anota cada despacho por la tabla (instrucción de origen, destino, veces, si
estaba en la tabla) para `tools/coverage.py`; `harness_set_cycle_trace(f, desde, hasta)` escribe «pc ciclo cuadro» de cada
instrucción de esos cuadros (comparación del reloj con MAME, D47). La cobertura y la traza se apuntan cuando la instrucción
de verdad va a ejecutarse (tras atender una IRQ que la interrumpe), como el depurador de MAME.

Sonido (hito 4): `machine_attach_sound(mc, placa)` instala el gancho de los latches en `Mem` (cada escritura del 68000 en
0x800181/0x800189 llega a `sound_latch_write` con `cpu.cycles`, el final de la instrucción) y hace que la placa corra hasta
el ciclo del 68000 en cada VBlank, antes de `on_vblank`. `machine_log_sound_cmds(mc, f)` anota en `f` cada escritura de los
latches como `cuadro latch valor` (la numeración de `tools/mame_sound_cmds.lua`) sin necesitar la placa ni sus ROMs.

### `sf2_blind` (hito 2): máquina CPS1 a ciegas (Musashi de reserva solo con `--fallback`)

Ejecuta solo el código recompilado sobre `runtime/cps1.c`: la IRQ2 se marca pendiente cada
`--frame-cycles` ciclos estimados (167.680 por defecto) y se entrega en el siguiente límite de
instrucción; cada entrega es un cuadro, al que se aplican las entradas de `--inputs`. Al llegar al
PC de muestra (0xAD0) escribe la línea del resumen en `--summary`. Si el control llega a una
dirección sin función recompilada, `rt_unknown()` pasa el estado a Musashi, que interpreta sobre la
misma memoria hasta volver a una dirección de la tabla, y el episodio se registra al final
(`codigo ejecutado por Musashi: entrada -> salida, instrucciones, episodios`); con ello se añaden
`entry` a `tools/hints.txt`. Las IRQ durante un episodio las atiende igualmente el manejador
recompilado.

Opciones: `--frames N`, `--inputs archivo`, `--summary archivo` (`-` = stdout), `--detail`,
`--dump-frame N --dump-dir dir` (vuelca `wram_N.bin` y `gfx_N.bin`), `--io-table archivo`,
`--frame-cycles N`, `--insns N`, `--cov archivo`, `--log-io`, `--quiet`, `--step` (ejecuta con `machine_step`). Vídeo (hito 3): `--png-frames
a,b,c` renderiza cada VBlank y escribe `<png-dir>/<png-prefix>_N.png` (384 x 224 RGB) en el punto de
muestra de cada cuadro de la lista, con los registros CPS-A/CPS-B y los contadores de prioridad en
stderr; `--gfx dir` (por defecto `sf2`) es el directorio de las ROMs gráficas. Sonido (hito 4): `--audio`
conecta la placa de sonido (`--sound-roms dir`, por defecto `sf2`); `--audio-dir dir` escribe ahí
`events.txt`, `ym.f32`, `oki.f32`, `mono.f32` y `chunks.txt` en el formato de `tools/mame_audio.lua`, y
`--wav archivo` la mezcla como WAV PCM de 16 bits a 48 kHz; las dos activan `--audio`. `--sound-cmds archivo`
escribe solo los comandos del 68000 a los latches (`cuadro latch valor`, el formato de
`tools/mame_sound_cmds.lua`) y no necesita las ROMs de sonido.

Hito 6: `--dips aa,bb,cc` (dipswitches en hexadecimal), `--fallback` (red de seguridad de Musashi), `--dispatch-log archivo`,
`--trace-cycles desde:hasta archivo`, y `--roundtrip a,b,c` (implica `--step`): en esos cuadros guarda el estado completo,
reinicia la máquina con la RAM llena de basura y carga el estado; el resumen tiene que salir idéntico al de la partida
seguida (`tools/coverage.py --roundtrip` lo comprueba en todas las secuencias, también el WAV).

### `sf2_lockstep`: paso cerrado con Musashi

El código recompilado lleva la iniciativa. Antes de cada instrucción llama a `rt_hook()` (`main.c`),
que:

1. Entrega la IRQ2 pendiente en los dos lados si la máscara lo permite: en el recompilado apila el
   marco y ejecuta el manejador anidado hasta su `rte`; en Musashi fuerza `m68ki_check_interrupts()`
   (`musashi_glue.c`) sin ejecutar nada. Antes aplica las entradas del cuadro a las dos memorias.
2. Compara d0-d7, a0-a7, PC y SR, y el conjunto de escrituras de 16 bits de la instrucción anterior
   (ordenado por dirección). Cada 60 cuadros compara además la RAM completa.
3. Ejecuta una instrucción en Musashi (`m68k_execute(1)`), acumula sus ciclos y, cada 167.680
   ciclos (el cuadro de 59,637 Hz de MAME), marca la IRQ2 pendiente en ambos lados.

Cada lado tiene su propia instancia `Mem` (RAM separada) porque una instrucción read-modify-write
sobre memoria compartida contaminaría al otro lado. La ROM se comparte.

Opciones: `--frames N` (periodos de VBlank; criterio del hito 1), `--irq-frames N` (entregas de IRQ2,
termina al llegar), `--inputs`, `--summary`, `--detail`, `--frame-cycles`, `--io-table`, `--insns N`,
`--trace`, `--cycle-check` (compara el coste estimado de cada instrucción con el de Musashi e
imprime un histograma por mnemónico), `--log-io`, `--quiet`, `--cov archivo`.

Detalles de arranque: Musashi parte con Z=1 (su `not_z_flag` vale 0) y su primer `m68k_execute`
solo consume los ciclos de la secuencia de reset; el arnés replica ambas cosas. Musashi toma la IRQ
dentro de la instrucción que la desenmascara (`move #$2000,sr`, `rte`), el recompilado en el límite
siguiente; `--cycle-check` lo tiene en cuenta.

## Informe de divergencia

```
DIVERGENCIA (pc/sr) en el cuadro 0, instruccion 2, 40 ciclos
siguiente instruccion (recomp)  000416: move.b  #$0, $800030.l
siguiente instruccion (musashi) 00040e: move.b  #$80, $800030.l
ultima instruccion ejecutada    000416: ...
recomp    pc=000416 sr=2708
          d0-d7: ...
musashi   pc=00040e sr=2704
  sr difiere: recomp 2708 musashi 2704
  escrituras recomp: [800030]=8000
ultimas instrucciones (recompilado):
  ...64 direcciones con desensamblado...
```

Códigos de salida: 0 sin divergencia, 1 divergencia, 2 error fatal (destino de salto sin resolver
o sin etiqueta: añadir a `tools/hints.txt`), 3 memoria distinta al final.

## Resultados de referencia (2026-10-19, hito 2)

```
make check
OK: 600 cuadros sin divergencia (494 entregas de IRQ2), 9382741 instrucciones, 100608006 ciclos, memoria identica

build/sf2_lockstep build/rom.bin --irq-frames 2000 --inputs tools/inputs_sf2.txt --cycle-check --quiet
OK: 2106 cuadros sin divergencia (2001 entregas de IRQ2), 31600878 instrucciones, 353134160 ciclos, memoria identica
reloj estimado por el codigo generado: 353134160 ciclos (Musashi 353134160, diferencia 0)
comprobacion de ciclos: 31598877 instrucciones, 0 con coste distinto
```

(Las cifras del hito 1 eran con cuadros de 166.666 ciclos: 600 cuadros, 493 IRQ, 9.330.968
instrucciones.)

## Archivos

- `main.c`: arnés en paso cerrado (`sf2_lockstep`): gancho, entrega de IRQ en ambos lados, informe de
  divergencia, comprobación de ciclos, resumen por cuadro. Trae su propia máquina y sus ganchos `rt_*`.
- `harness.c/.h`: cobertura, anillo de traza, Musashi de reserva e informe final sobre la máquina del núcleo.
- `blind.c`: línea de órdenes de `sf2_blind` (resumen por cuadro, tabla de E/S, PNG sin ventana, sonido).
- `common.c/.h`: cobertura dinámica, anillo de las 64 últimas direcciones, carga de `rom.bin`.
- `musashi_glue.c/.h`: callbacks de memoria de Musashi sobre `runtime/mem.c` (a través de
  `g_musashi_mem_ptr`: memoria propia en paso cerrado, la del juego en el arnés a ciegas), acuse de
  IRQ, desensamblador para los informes.
- `stubs_dasm.c`: callbacks mínimos para `tools/dasmcheck.c` y `tools/dump_cycles.c`.

Desaparecen `machine.c/.h` (ahora `runtime/machine.c/.h`) y `view.c` (el visor `sf2_video` es ahora el juego `app/`,
ver `app/README.md`). El antiguo `sf2_run` (modo autónomo sin Musashi del hito 1) quedó sustituido por `sf2_blind`.
