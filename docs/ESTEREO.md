# Sonido estéreo (mod)

La placa CPS1 tiene un solo altavoz: MAME, y este port, mezclan el YM2151 (música) y el OKI MSM6295 (voces, golpes,
proyectiles) en mono. Con este mod las frases del OKI suenan **del lado de la pantalla donde ocurren**: el hadoken de Ryu a la
izquierda si Ryu está a la izquierda, el golpe que recibe Ken a la derecha, el grito del KO del lado del que cae. La música y los
sonidos sin posición (locutor, menús, monedas, la percusión de la música) siguen al centro.

**Solo presentación:** no cambia la partida ni el estado guardado. **Apagado, el audio es el original**, mono e idéntico a MAME bit a
bit (D61).

| Cómo | |
|---|---|
| Menú | Sonido ▸ Estéreo (mod) y Separación estéreo (0 a 100 %, de 10 en 10) |
| Configuración | `[audio]` `stereo = true`, `stereo_width = 100` |
| Línea de órdenes | `build/sf2 --stereo [--stereo-width 0-100]` |
| Arnés | `build/sf2_blind rom.bin --stereo --wav salida.wav` (WAV estéreo) |
| Núcleo | `sf2_set_stereo(s, 1)`, `sf2_set_stereo_width(s, 0-100)`; `sf2_audio_channels(s)` dice si `sf2_audio` entrega 1 o 2 canales |

## Cómo funciona

1. **Quién pide cada sonido (68000).** El juego no habla con el Z80 directamente: encola cada comando en una cola de 64 palabras
   en `0xFF81A2` (rutina `0x62A4`, índice de escritura `0x22(a5)`, de lectura `0x24(a5)`) y la rutina `0x6264` saca uno por llamada y lo
   escribe en los latches `0x800189`/`0x800181`. Al encolar, `a6` apunta al objeto que suena: los luchadores (`0xFF83C6`, `0xFF86C6`) o un
   objeto del combate (proyectiles y chispas de golpe, `0xFF9000-0xFFDFFF`); medido con MAME (un tap de escritura sobre la cola). Su X de
   mundo está en `0x6(a6)` y el borde izquierdo de la cámara en `0xFF8BD8`.
2. **Parche en el recompilador.** `recompiler/mod_patches.txt` añade en `0x62AC` (el `move.w d0,0x0(a4,d1.w)` que encola) una llamada a
   `mod_sound_tag` (`runtime/cpu.h`) solo si `c->mod_stereo`. Anota en campos del anfitrión (`snd_x`, `snd_ok`, uno por ranura de la cola)
   la X en pantalla del objeto; lee la RAM directamente, sin pasar por el bus, y no escribe nada de la máquina.
3. **Del latch a la voz (placa de sonido).** Cuando el 68000 escribe el latch 1, `runtime/machine.c` toma la posición de la ranura que se
   está sacando y la mete en la cola de latches de `runtime/sound.c` junto al comando. Al aplicarse un comando (que no sea `0xFF`, el «sin
   comando» que sigue a cada uno) queda como el último con posición; cuando el Z80 arranca una frase en una voz del OKI, la voz toma el
   paneo de ese comando si tiene menos de 50 ms y cae dentro de la vista (con 32 px de margen). Si no, al centro.
4. **Mezcla.** El OKI se sigue generando igual (`oki6295_generate_voices` devuelve la misma suma que `oki6295_generate`, con las mismas
   operaciones) y además llena dos anillos, `oki_l` y `oki_r`, con cada voz multiplicada por su ganancia de lado. Ley de balance: la voz
   suena entera en su lado y al `1 − separación·|paneo|` en el contrario, así que una voz al centro suma lo mismo que en la mezcla
   original. La separación es el ajuste `stereo_width` (100 % por omisión, `SOUND_PAN_WIDTH`: en el borde de la pantalla el lado contrario
   calla; con 0 % las dos salidas son la mezcla mono exacta) y se aplica en cada muestra, también a las frases que ya suenan. Cada lado se remuestrea con el mismo remuestreador lofi de MAME y se suma al YM2151 en el mismo orden que la mezcla mono:
   **con todas las voces al centro, L y R son exactamente la mezcla original**.

El paneo se calcula con la media anchura de la vista (`sound_set_pan_half`, 192 px; con pantalla ancha, la mitad del ancho del cuadro), así
que con 16:9 o ultraancho el borde de la ventana es el extremo.

## Comprobaciones (2026-11-04)

- Mod apagado: `mono.f32`, `oki.f32`, `ym.f32`, `events.txt` y `chunks.txt` de `sf2_blind --audio-dir` (9.000 cuadros de `ryu.txt`)
  idénticos byte a byte a los de antes del cambio; `make compare-audio` «todo igual» contra MAME; `make test` 8/8.
- Mod encendido: el resumen por cuadro (`--summary`) y la lista de comandos de sonido (`--sound-cmds`) idénticos a los del mod apagado en
  20.000 cuadros de `ryu.txt` y de `ken.txt`: la partida no cambia.
- WAV estéreo de `ryu.txt`: 5,3 millones de muestras con L = R = mono exacto (voces al centro o sin OKI); las 2 millones restantes, en los
  tramos con voces paneadas. Posiciones medidas en combate entre 57 y 347 px.
- Separación (fachada, 6.000 cuadros): con 0 % las 4.828.801 muestras tienen L = R = mono; con 100 %, 1.333.178 con L ≠ R.
- Aplicación (`SDL_AUDIO_DRIVER=disk`): con `--stereo` el dispositivo recibe L ≠ R; sin él, L = R en todas las muestras.

## Street Fighter Zero (CPS Changer, D73)

Lo que necesita el mod está en un descriptor propio por variante (`StereoRam` en `runtime/game.h`/`game.c`), aparte del resto de los mods,
así que sfzch lo tiene sin tener los demás. Diferencias con SF2, medidas en MAME con un tap de escritura sobre la cola:

| | SF2 (WW) | sfzch |
|---|---|---|
| Instrucción que encola (parche) | 0x62AC `move.w d0,0x0(a4,d1.w)` | 0x11364 `move.w d1,0x0(a4,d0.w)` |
| Cola | 64 palabras en 0xFF81A2, ranura en d1 | 256 palabras en 0xFF261A, ranura en d0 |
| Quién lo pide (a6) | luchador + 6 (0xFF83C6, 0xFF86C6) u objeto 0xFF9000-0xFFDFFF | luchador (0xFF8400, 0xFF8800) u objeto 0xFF9000-0xFF9FFF |
| X de mundo | 0x6(a6) | 0x10(a6) |
| Cámara | 0xFF8BD8 | 0xFF8290 |
| Índice de lectura | 0x24(a5) | -0x49E4(a5) = 0xFF361C |
| «Sin comando» tras cada sonido | latch 1 = 0xFF | palabra 0xFF00 (latch 1 = 0x00) |

- **«Sin comando».** En sfzch el latch 1 del «sin comando» vale 0x00, que la placa tomaba por un comando nuevo sin posición: un cuadro
  después de cada sonido le quitaba el paneo antes de que el Z80 arrancara la frase. `machine.c` lee la palabra de la ranura que se saca y,
  si es 0xFF00, la marca (`SOUND_POS_NOCMD`) para que no toque la posición.
- **Solo en juego.** En la selección los «luchadores» piden los sonidos con la cámara a 0; en sfzch solo se panea si el luchador 1 está en
  juego (byte 0x4 de su estructura = 2: partida, combates de la demostración y el combate de la intro).
- Comprobado con una partida de hadokens (3.600 cuadros): L ≠ R en el combate y en los de la demostración, con más energía del lado de
  Ryu; con el mod encendido la partida es la misma (`sf2_game_crc` y `sf2_state_crc` iguales, `test_no_mods`), y apagado el audio sigue
  idéntico a MAME.

## Límites

- El paneo de una frase se fija al empezar; no sigue al objeto mientras suena.
- El enlace comando → frase es por tiempo (el último comando con posición, de menos de 50 ms). Dos sonidos casi simultáneos de lados
  distintos pueden tomar el paneo del más reciente.
- En los bonus algunos objetos usan otras coordenadas: quedan fuera del rango y suenan al centro.
- La música (YM2151) no se toca: el programa del Z80 la escribe para un altavoz mono.
