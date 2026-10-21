# 2026-10-19 · Hito 4 · Referencia de sonido desde MAME (`tools/mame_audio.lua`)

## Pregunta de partida

Qué se puede sacar del MAME local para comparar el sonido "igual que en MAME" sin quedarse en una
comparación a oído o espectral: la mezcla final pasa por un remuestreador y depende del orden de
operaciones en float, así que una diferencia ahí no dice si el fallo está en el Z80, en un chip o en la
mezcla.

## Qué se hizo

Leído el driver (`src/mame/capcom/cps1.cpp`, `cps1_10MHz` y `sub_map`) y el sistema de sonido de MAME
0.289 (`emu/sound.cpp`, `emu/resampler.cpp`, `devices/sound/ymfm_mame.h`, `okim6295.cpp`):

| Pieza | En MAME |
|---|---|
| CPU de sonido | Z80 a 3,579545 MHz, `sub_map`: ROM 0x0000-0x7FFF, banco 0x8000-0xBFFF, RAM 0xD000-0xD7FF, YM2151 0xF000-1, OKI 0xF002, banco 0xF004, pin 7 0xF006, latches 0xF008/0xF00A |
| YM2151 | `ym2151_device` = ymfm (`3rdparty/ymfm`), mismo cristal que el Z80, IRQ a la línea INT del Z80; salidas 0 y 1 al altavoz con ganancia 0,35 |
| OKI | MSM6295 a 1 MHz con el pin 7 alto (7.575 Hz), ROM de 256 KiB (`sf2_18` + `sf2_19`), ganancia 0,30 |
| Latches | `generic_latch_8` ×2: el 68000 escribe en 0x800181 (comando) y 0x800189 ("fade") |
| Mezcla | altavoz mono a la frecuencia de la máquina (48 kHz), remuestreador "lofi" por defecto, actualización periódica a 50 Hz |

`tools/mame_audio.lua` (mismas entradas y numeración de cuadros que `mame_blind.lua`) escribe en
`SF2_AUDIO_DIR`:

- `events.txt`: `F n t68` en el punto de muestra de cada cuadro; `L k v t68 tz` por cada escritura del
  68000 en un latch (tap de escritura en 0x800180-0x80018F, con la regla de `cps1_soundlatch_w`: byte bajo,
  o alto si solo se escribe el alto); `W a v tz` y `R a v tz` por cada escritura y lectura del Z80 en
  0xF000-0xF00F (taps en el espacio del `:audiocpu`, la lectura con el valor leído).
- `ym.f32`, `oki.f32`, `mono.f32`: lo que entrega `emu.register_sound_update` para los dispositivos con
  `hook = true` (`:2151`, `:oki`, `:mono`): los flujos de salida de los chips tal cual los generan (antes de
  remuestrear) y el búfer de entrada del altavoz, que es exactamente lo que `-wavwrite` graba
  (`sound_manager::output_push`). `chunks.txt` guarda cuántas muestras trae cada actualización.

### Tiempo de cada acceso

Dentro de un tap, `manager.machine.time` es `device_scheduler::time()`, que con una CPU ejecutándose
devuelve su tiempo local (`local_time`: tiempo de la porción + ciclos consumidos). El núcleo Z80 de 0.289
(`z80.lst`, generado por `z80make.py`) hace cada acceso **antes** de descontar los ciclos de su ciclo M
(`N !! acceso` = acceso, luego `icount -= N`), así que el tiempo del tap es el comienzo del ciclo M del
acceso. Ese es el instante que el runtime tiene que reproducir.

MAME acumula el tiempo con attosegundos por ciclo truncados (1e18 / 3579545 = 279365114840,6 → …840),
así que `as_ticks(3579545)` da un ciclo menos; el script redondea `segundos·f + attosegundos/1e18·f` al
entero más próximo. El 68000 (1e11 as/ciclo) es exacto.

### WAV de MAME y lista de comandos

- `-wavwrite archivo.wav` graba en la misma pasada el WAV de la mezcla del altavoz (PCM de 16 bits, 48 kHz,
  mono) y funciona con la invocación `MAME_HEADLESS` (`-video none -sound none … -nothrottle`): el sistema de
  sonido sigue corriendo sin dispositivo de salida. `make mame-audio` lo pasa, y `tools/compare_audio.py`
  comprueba que ese WAV es la cuantización de `mono.f32` del gancho de Lua (misma fuente).
- `tools/mame_sound_cmds.lua` es la versión mínima del registro (solo las escrituras del 68000 en los latches,
  por cuadro, en `cuadro latch valor`) para el primer nivel de validación (`make compare-sound-cmds`).

```bash
SF2_AUDIO_DIR=$PWD/build/audio/mame SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
  tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
    -joystickprovider none -lightgunprovider none -str 240 -wavwrite $PWD/build/audio/mame/sf2_mame.wav \
    -autoboot_script tools/mame_audio.lua
make mame-audio          # lo mismo con BFRAMES e INPUTS del Makefile
SF2_CMDS=$PWD/build/audio/cmds_mame.txt SF2_INPUTS=tools/inputs_sf2.txt SF2_FRAMES=12000 \
  tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
    -joystickprovider none -lightgunprovider none -str 240 -autoboot_script tools/mame_sound_cmds.lua
```

## Resultados

- 300 cuadros en 3 s de reloj; los 12.000 en ~20 s (1.050 % de velocidad): 1.391 escrituras de latch,
  456.589 escrituras y 316.409 lecturas del Z80, 11.351.553 muestras del YM2151, 1.537.423 del OKI y
  9.742.081 de la mezcla (203 s). 163 MB en `build/audio/mame/`.
- Con `-sound none` el sistema de sonido sigue funcionando (flujos, ganchos y `-wavwrite`).
- La primera actualización periódica trae 1.119 muestras del YM, 152 del OKI y 961 del altavoz:
  floor(0,02 · f) + 1, es decir, los flujos empiezan en la muestra 0 en t = 0 y cada actualización genera
  hasta la muestra del instante actual inclusive (`sound_stream::update`).
- Lo que hace el Z80 se ve enseguida: el manejador de INT (0x38) lee el latch en cada interrupción del
  temporizador A del YM2151 (≈250 Hz) y el bucle principal espera a que el contador de interrupciones
  cambie (detalle en `2026-10-19-hito4-z80.md`).

## Pendiente

Nada en este paso; la comparación está en `2026-10-19-hito4-comparacion-y-cierre.md`.
