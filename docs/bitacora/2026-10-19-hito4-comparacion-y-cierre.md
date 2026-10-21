# 2026-10-19 · Hito 4 · Comparación con MAME (comandos, accesos, flujos y WAV), sonido en el visor y cierre del hito

## Pregunta de partida

Cómo demostrar el criterio del plan ("música y efectos del arranque y de un combate suenan igual que en
MAME, comparación de volcados de audio") de forma que una diferencia, si la hay, diga dónde está.

## Qué se hizo

Dos niveles, como pedía el hito: primero el flujo de comandos del 68000 al Z80, después el audio. El segundo
está a su vez en capas (decisión D32), de modo que una diferencia en la mezcla señala su causa.

### Nivel 1: flujo de comandos (`make compare-sound-cmds`)

- `tools/mame_sound_cmds.lua`: en MAME registra, por cuadro, cada escritura del 68000 en los latches de sonido
  (taps de escritura en 0x800180-0x80018F con las reglas de `cps1_soundlatch_w`/`cps1_soundlatch2_w`), con la
  misma secuencia de entradas y numeración de cuadros que `mame_blind.lua`. Salida `cuadro latch valor` más la
  línea `# cuadros N`.
- `build/sf2_blind --sound-cmds archivo`: la misma lista desde el runtime (`harness/machine.c`,
  `machine_log_sound_cmds`). No carga la placa de sonido ni sus ROMs, así que se puede comparar el flujo de
  comandos sin ellas. El cuadro es `sample_frame`, el último punto de muestra pasado (como `A.frame` de Lua).
- `tools/compare_sound_cmds.py`: compara en orden (latch, valor, cuadro) los comandos de los cuadros anteriores a
  min(N_a, N_b) (los del último cuadro pueden estar incompletos porque cada lado para en un instante distinto),
  imprime un resumen del flujo por tramos de la partida y, si hay diferencia, la **primera** con tres comandos de
  contexto a cada lado. Código de salida 0 o 1. Probado con dos errores inyectados (un valor cambiado y un
  comando movido de cuadro): señala el primero (`#500`, cuadro 4023, `ab` frente a `ff`) y cuenta los dos.

### Nivel 2: audio (`make compare-audio`)

#### `tools/compare_audio.py`

Compara dos directorios con el formato de `tools/mame_audio.lua` (runtime: `sf2_blind --audio-dir`), en
capas, cada una explicando la siguiente (decisión D32):

1. Cuadros: diferencia del instante del punto de muestra (informativa).
2. Comandos del 68000: valor, latch y cuadro de cada escritura; diferencia de instante (informativa).
3. Accesos del Z80 a 0xF000-0xF00F: escrituras (dirección, valor, ciclo) y lecturas (dirección, valor
   leído, ciclo), ordenadas por ciclo (MAME las registra en orden de planificador). Imprime el contexto de
   la primera diferencia con el cuadro de cada acceso.
4. Flujos `ym.f32`, `oki.f32` y `mono.f32`: igualdad bit a bit por bloques, primera muestra distinta,
   diferencia máxima y señal/ruido; con `--sections`, resultado por tramos de la partida
   (`tools/inputs_sf2.txt`).
5. **WAV contra WAV, por tramos, con energía y espectro** (D33): el WAV del runtime (`--wav`) frente al que graba
   MAME con `-wavwrite` en la misma pasada que los registros (`make mame-audio` pasa `-wavwrite` a la
   invocación `MAME_HEADLESS`). Es la comparación que se sigue pudiendo hacer cuando no se espera igualdad
   muestra a muestra (otra frecuencia de salida u otro remuestreador, otro compilador): para cada tramo de la
   partida (arranque, aviso y atract, moneda, selección, combate, KO y continuar, atract y demo) se mide
   - **energía**: RMS en dBFS de cada lado; pasa si la diferencia es ≤ 0,5 dB (un tramo con RMS < −80 dBFS cuenta
     como silencio: dos silencios son iguales aunque difieran en 1 LSB, que son −90 dBFS);
   - **espectro**: hasta 32 ventanas de 2048 muestras con Hann, repartidas por las partes con señal del tramo, con
     el espectro de potencia promediado. Pasa si la energía de cada banda de octava (9 bandas, 0-100 Hz hasta
     12,8-24 kHz) difiere ≤ 1,0 dB, solo en las bandas a menos de 60 dB de la más fuerte, y si la distancia
     espectral logarítmica (raíz de la media del cuadrado de la diferencia en dB por bin, en los bins a menos de
     60 dB del máximo) es ≤ 1,0 dB;
   - informativos: muestras distintas, diferencia máxima en LSB y relación señal/ruido. No se exigen.
   Sin dependencias (FFT radix-2 en Python, dos señales reales por FFT compleja; ~8 s para los 203 s).
   `--report archivo` escribe el informe en Markdown (`build/audio/informe_audio.md`: tabla por tramo, bandas
   y la salida completa). También comprueba que el WAV de MAME sea la cuantización de `mono.f32` del gancho de
   Lua (la referencia es una sola cosa), y acepta dos WAV sueltos: `python3 tools/compare_audio.py a.wav b.wav`.

Los dos lados paran en instantes distintos (MAME en el punto de muestra del último cuadro, el runtime al
entregar la IRQ siguiente): se compara hasta el último punto de muestra común. Sin dependencias (`array`
y comparación de bytes; 3,5 s para 160 MB). Código 0 si todo coincide.

```bash
make compare-audio        # run-audio + mame-audio (si cambian script, entradas o BFRAMES) + comparación
python3 tools/compare_audio.py build/audio/recomp build/audio/mame --sections
```

### Pruebas de que los criterios del WAV discriminan

Con `tools/compare_audio.py` sobre copias alteradas del WAV de MAME (no se versionan; generadas con un script de
usar y tirar), resultado por tramo con señal:

| Alteración | Energía | Bandas | LSD | Resultado |
|---|---|---|---|---|
| ninguna (WAV del runtime) | 0,000 dB | 0,000 dB | 0,000 dB | pasa, 0 muestras distintas |
| ganancia ×0,85 (−1,4 dB) | −1,41 dB | 1,41 dB | 1,41 dB | **falla** en todos los tramos |
| ±1 LSB en el 2 % de las muestras (redondeo de otra plataforma) | 0,000 dB | ≤ 0,012 dB | ≤ 0,007 dB | pasa (SNR 73-90 dB) |
| desplazado 1 muestra (20 µs) | 0,000 dB | ≤ 0,021 dB | ≤ 0,016 dB | pasa (SNR 16-23 dB) |
| paso bajo (media de 2 muestras) | ≥ −0,03 dB | 5,3-7,6 dB | 0,9-2,8 dB | **falla** |
| desplazado 0,5 s | ≤ 0,14 dB | 1,5-6,4 dB | 2,3-7,5 dB | **falla** |

El segundo caso destapó un defecto de la primera versión del criterio: los tramos en silencio de MAME eran
silencio digital exacto y los del WAV con ±1 LSB no, y la regla "uno en silencio y otro no" los suspendía con
diferencia infinita; por eso el umbral de silencio es −80 dBFS y no "todo ceros".

### Sonido en el visor

`sf2_video` carga la placa de sonido (ROMs de `--sound-roms`, por defecto las de `--gfx`) y manda cada
bloque de 48 kHz a un `SDL_AudioStream` (F32 mono). El ritmo lo sigue marcando el vídeo a 59,637 Hz; si la
cola pasa de 150 ms se descarta el bloque. `--mute` lo desactiva y con `--nothrottle` no se reproduce. Al
terminar imprime la cola máxima y las muestras descartadas.

Probado sin altavoz ni ventana con los controladores de SDL, 1.800 cuadros (32 s de máquina: moneda, selección
de personaje y arranque del combate):

- `SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=disk` hace que SDL escriba lo que le llega al dispositivo en
  `sdlaudio.raw` del directorio actual (F32 estéreo, la entrada mono duplicada; en SDL3 el nombre se cambia
  con `SDL_AUDIO_DISK_OUTPUT_FILE`, no con `SDL_DISKAUDIOFILE`). Se buscó cada bloque de la mezcla de referencia
  (`build/audio/recomp/mono.f32`) en el archivo: los 638 bloques con señal que SDL llegó a reproducir son
  **idénticos bit a bit** a los del runtime, en orden y alineados a muestra. Los otros 50 bloques con señal
  (de 688) no están: los descartó el visor (ver abajo).
- Con `dummy` y con `disk` el visor descartó entre el 6,0 % y el 6,6 % del audio (cola máxima 164 ms, por encima
  de los 150 ms del umbral): la cola crece unos 0,8 ms por cuadro, es decir, el dispositivo consume ≈94 % del
  tiempo real. Los dos controladores simulan el dispositivo con esperas y no con un reloj de hardware, así que
  es un artefacto de la prueba y no del flujo de datos, pero también la señal de que **el ritmo lo marca solo
  el vídeo**: con un dispositivo real cuyo reloj difiera del del anfitrión, la cola crecerá hasta el umbral y
  se perderá un bloque de 20 ms cada cierto tiempo. No se ha probado con un dispositivo de audio real. Queda
  para el hito 5 (ver Pendiente).

### Sin regresiones

`make check` (paso cerrado contra Musashi, 600 cuadros) sigue sin divergencias y `make compare-video` da
17 de 17 cuadros idénticos. El único cambio en la parte del 68000 es `mem.c`: el gancho de los latches y
la escritura del byte alto en el latch 1 (`cps1_soundlatch_w`), que SF2 no usa.

## Resultados

Partida de referencia de 12.000 cuadros (`tools/inputs_sf2.txt`: arranque, atract, moneda, selección de
Ryu, combate contra Blanka, KO, continuar, fin de partida, atract y demo), 202,98 s de máquina:

| Capa | Runtime | MAME | Resultado |
|---|---:|---:|---|
| Comandos del 68000 (latches) | 1.391 | 1.391 | todos con el mismo valor y en el mismo cuadro |
| Escrituras del Z80 en 0xF000-0xF00F | 456.589 | 456.589 | todas iguales en dirección, valor y ciclo |
| Lecturas del Z80 en 0xF000-0xF00F | 316.409 | 316.409 | todas iguales en dirección, valor leído y ciclo |
| YM2151, 55.930 Hz, 2 canales | 11.351.553 | 11.351.553 | idénticas bit a bit |
| OKI, 7.575 Hz | 1.537.423 | 1.537.423 | idénticas bit a bit |
| Mezcla del altavoz, 48 kHz | 9.742.081 | 9.742.081 | idénticas bit a bit |
| WAV de `-wavwrite` de MAME frente a `--wav` | 9.742.081 | 9.742.081 | idénticos byte a byte (los datos; las cabeceras difieren en la longitud) |
| WAV por tramos, energía y espectro (7 tramos) | | | 7 de 7 pasan; diferencia 0,000 dB en energía, bandas y LSD |
| Flujo de comandos (`make compare-sound-cmds`) | 1.391 | 1.391 | iguales en latch, valor y cuadro (696 comandos y 695 fades) |

Por tramos (`--sections`): arranque y test de RAM (0-1,8 s) y aviso/atract (hasta 11,8 s) en silencio en
los dos lados (el 68000 no manda más comando que el 0xF0 del arranque hasta la moneda); moneda, selección, combate
(26,9-63,8 s), KO/continuar/fin de partida y atract con demo, idénticos.

Historial: 7 ciclos de desfase en la INT (instante de la escritura aplazada del YM2151) → 400.706 muestras
de mezcla con 1-4 ulp (tabla de interpolación con FMA) → 1 muestra del OKI (orden de la actualización
periódica frente a un acceso en el mismo ciclo) → 0. Las tres causas y su corrección están en
`2026-10-19-hito4-planificacion.md` y `2026-10-19-hito4-mezcla.md`.

Tiempos: el runtime hace los 12.000 cuadros con sonido y todos los registros en ~6 s; MAME con
`mame_audio.lua` ~20 s; la comparación 3,5 s.

## Estado del hito 4

Cumplido el criterio del plan, con más margen del pedido: no solo suenan igual el arranque y un combate,
sino que la partida completa coincide bit a bit con MAME en cada nivel (comandos, accesos del Z80 al ciclo,
salida de cada chip y mezcla). Entregables: `runtime/z80.c`, `runtime/ym2151.cpp` sobre
`third_party/ymfm`, `runtime/oki6295.c`, `runtime/sound.c`, `runtime/wav.c`, el gancho de latches en
`runtime/mem.c` y su conexión en `harness/machine.c`, `sf2_blind --audio/--audio-dir/--wav`, sonido en
`sf2_video`, `tools/mame_audio.lua`, `tools/compare_audio.py`, `tools/z80dis.c`, objetivos `run-audio`,
`mame-audio` y `compare-audio`, decisiones D27-D32, READMEs y estas seis entradas.

Verificación independiente de esta entrega (segunda pasada del hito): `make compare-audio` con la referencia de
MAME regenerada de cero (otra ejecución de MAME, con `-wavwrite` incluido) da los mismos 1.391, 456.589,
316.409, 11.351.553, 1.537.423 y 9.742.081; el WAV de MAME es la cuantización de su `mono.f32`; y las regresiones
de los hitos anteriores siguen igual (`make check` 600 cuadros sin divergencia, `make compare-video` 17 de 17,
`make compare-mame` con los mismos 18 cuadros explicados del hito 2).

## Pendiente

- Visor: el ritmo de la salida lo marca el reloj de vídeo; para un anfitrión cuyo reloj de audio no coincida
  hace falta sincronizar con el audio (ajustar la espera del cuadro según la cola, o remuestrear de forma
  adaptativa) en lugar de descartar bloques. Hito 5.
- Riesgo conocido: un comando del 68000 escrito a menos de ~250 ciclos del 68000 de una lectura del latch
  por el Z80 podría caer al otro lado de ella por la diferencia entre el reloj de Musashi y el de MAME
  (`2026-10-19-hito4-planificacion.md`); no ocurre en la partida de referencia. Se resuelve con el reloj
  exacto del 68000 (hito 6).
- Pin 7 bajo del OKI y otras frecuencias de salida (no los usa SF2 / hito 5).
- Recompilar el Z80 (opcional).
- Hito 5: portabilidad (CMake, Linux, Windows, Web).
