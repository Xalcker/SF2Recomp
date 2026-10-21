# 2026-10-19 · Hito 4 · Mezcla a 48 kHz y WAV

## Pregunta de partida

Si se puede reproducir la mezcla de MAME bit a bit (el WAV de `-wavwrite`), o si la comparación final
tiene que ser aproximada.

## Qué se hizo

`sound.cpp` de MAME 0.289 mezcla así, y `runtime/sound.c` lo calca (decisión D31):

- El altavoz mono es un flujo a la frecuencia de la máquina (48.000 Hz). En cada actualización periódica
  (k / 50 s, 960 muestras salvo la primera, de 961) pide las muestras desde la siguiente a la última hasta
  floor(t · 48000).
- Cada ruta (YM2151 salida 0 y salida 1 con ganancia 0,35, OKI con 0,30, en ese orden de
  `m_bw_routes`) se remuestrea con `audio_resampler_lofi::apply` y se suma al búfer de entrada:
  interpolación de 4 puntos con f0(p) = (p − p³)/6 y f1(p) = p + (p² − p³)/2 tabulados en 4096 fases,
  fase de 24 bits recalculada al principio de cada bloque a partir del índice de destino, "2 muestras de
  latencia" (lee desde 4 · divide muestras antes), y media de 2 muestras para el YM (55.930 > 48.000, divide
  = 2; paso 9.774.475) y ninguna para el OKI (paso 2.647.654).
- La grabación (`output_push`) es `clamp(int(x · 32768))` a 16 bits; `runtime/wav.c` hace lo mismo.

### Bits en float

La primera comparación de la partida completa dio la mezcla distinta en 400.706 muestras de 9,7 millones
(señal/ruido 88 dB): casi todas de 1 a 4 ulp de redondeo (más en valores cercanos a 0, por cancelación), y
las mayores, de hasta 0,0069, alrededor de 52,76 s. Se miró el código máquina de las dos partes:

- `audio_resampler_lofi::apply` en el binario de MAME (`objdump -d external/mame/cps1`): `fmul`,
  `fmsub`, `fmadd`, `fmsub` y `fmadd` (clang contrae a FMA por defecto en arm64). La misma expresión en C
  compilada con clang da la misma secuencia en `build/sound.o`: el remuestreo ya coincidía.
- La tabla de interpolación (`interpolation_table`) no se calcula al arrancar: clang la evalúa al compilar
  y está en `__TEXT,__const` del binario. Leída del archivo y comparada con la nuestra: 765 de 8.194
  valores distintos con la contracción de clang (`on` u `fast`), 0 con `-ffp-contract=off`. La
  evaluación en tiempo de compilación no contrae. `interp_init` lleva ahora
  `#pragma STDC FP_CONTRACT OFF`; el resto del archivo se compila con la contracción por defecto, como MAME
  (`SNDFLAGS` = `-O2`, sin opciones de float).

Tras eso solo quedaban 25 muestras distintas alrededor de 52,76 s, todas explicadas por la única muestra
distinta del OKI (detalle 2 de `2026-10-19-hito4-planificacion.md`), y con esa corrección 0.

### Salidas

`sf2_blind --wav archivo` escribe la mezcla en WAV PCM de 16 bits, 48 kHz, mono; `--audio-dir` escribe
además los flujos en float (`ym.f32`, `oki.f32`, `mono.f32`) y `chunks.txt` en el formato de
`tools/mame_audio.lua`.

## Resultados

- `mono.f32` (la mezcla en float): 9.742.081 muestras idénticas bit a bit a las del gancho de sonido de
  MAME.
- Comprobación independiente del gancho: MAME con su propio `-wavwrite` (`tools/mame_blind.lua`, la misma
  partida) frente a `build/audio/recomp/sf2.wav`: mismo formato (PCM 16 bits, 1 canal, 48.000 Hz) y las
  9.742.081 muestras comunes idénticas byte a byte (el runtime tiene 960 más porque para en el cuadro
  siguiente).

```bash
make run-audio                                       # build/audio/recomp/{events.txt,*.f32,sf2.wav}
tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
  -joystickprovider none -lightgunprovider none -str 240 -wavwrite $PWD/build/audio/mame_wav/sf2_mame.wav \
  -autoboot_script tools/mame_blind.lua              # con SF2_SUMMARY, SF2_INPUTS y SF2_FRAMES=12000
```

## Pendiente

- La mezcla está atada a 48 kHz y al remuestreador "lofi" porque es lo que se valida; otra frecuencia de
  salida (o el remuestreador "hq" de MAME) se puede añadir en el hito 5 sin tocar los chips.
