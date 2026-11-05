# 2026-11-04 · Mod de sonido estéreo

## Pregunta de partida

Un intento anterior de otra sesión había quedado sin compilar (enumeradores duplicados, funciones inexistentes) y con fallos de diseño: paneo fijo
por número de voz, la mezcla mono original borrada (silencio con el mod apagado) y el OKI generado a la frecuencia de salida, fuera de su reloj.
Se guardó en un `git stash` («stereo-mod intento fallido») y se rehízo desde cero con la regla de siempre: apagado = juego original.

## Qué se hizo

- Sonda en MAME (tap de escritura sobre la cola `0xFF81A2` filtrado por `CURPC = 0x62AC`; en los taps la pila es `SP`, no `A7`): `a6` al encolar es
  el luchador (`0xFF83C6`/`0xFF86C6`) o un objeto del combate (`0xFF9000-0xFFDFFF`); los demás valores (`0xFF8C52`, `0xFF0A10`…) son tareas sin
  posición. X de objeto en `0x6(a6)` y cámara en `0xFF8BD8` (la rutina de paredes `0x2D98` las usa igual).
- Parche `0x62ac` en `recompiler/mod_patches.txt` → `mod_sound_tag` (`runtime/cpu.h`); `machine.c` pasa la posición de la ranura que sale
  (`0x24(a5)`) con el latch 1 (`sound_latch_pos`); `sound.c` la aplica a las voces que arrancan; `oki6295_generate_voices`; mezcla L/R.
- Fachada: `sf2_set_stereo`, `sf2_stereo`, `sf2_audio_channels`. Aplicación: `[audio] stereo`, `--stereo`, Sonido ▸ Estéreo (mod), dispositivo
  en estéreo. Arnés: `sf2_blind --stereo --wav`.

## Verificación

- Apagado: salidas de audio de `sf2_blind --audio-dir` idénticas byte a byte a las de antes del cambio (9.000 cuadros de `ryu.txt`);
  `make compare-audio`: todo igual a MAME; `make test` 8/8; `--menu-test` OK.
- Encendido: `--summary` y `--sound-cmds` idénticos al modo apagado en 20.000 cuadros de `ryu.txt` y `ken.txt`.
- En el WAV estéreo las voces centradas dan L = R = mono exacto; un tramo de 2 s cargado a la derecha resultó ser el grito del KO del rival
  (comando 0x3D desde x = 300).
- Aplicación sin ventana con `SDL_AUDIO_DRIVER=disk`: L ≠ R con `--stereo`, L = R sin él.

## Arreglo de `SF2_USE_MODS`

`SF2_USE_MODS=OFF` (commit 772dd54) no desactivaba los parches: sin `--mods`, `gen_c.py` usaba `mod_patches.txt` por omisión; con ON, `--mods ruta`
iba como un solo argumento. Arreglado: `gen_c.py --no-mods`; `cmake/SF2Pipeline.cmake` pasa `--mods` y la ruta como lista o `--no-mods`, y escribe la
opción en `<build>/gen_mods.txt` (solo si cambia) como dependencia, porque el generador de Makefiles no repite una regla cuando cambia su orden;
el sub-proyecto de las builds cruzadas (`recompiler/host`) recibe la opción. Comprobado en builds aparte: OFF genera 0 usos de parches y ON 37
instrucciones parcheadas; alternar la opción en un mismo árbol regenera el C; la build OFF da `mono.f32` y `events.txt` idénticos a la línea base.
Sin probar: la build cruzada (Web/Windows).

## Prueba del usuario y ajustes

- Lo escuchó: «se escuchó bien». Probó la separación de 0,8 a 1,0 y se quedó con 1,0; se añadió el ajuste `stereo_width` (menú Sonido ▸
  Separación estéreo, `--stereo-width`), 100 % por omisión. Con 0 % la salida es la mezcla mono exacta.
- Un supuesto crash al reiniciar tras cambiar el ajuste rápido a «Original» era la pausa: había pausado antes, el reinicio no la quitaba y la
  máquina quedaba parada en la pantalla negra del arranque (salida con código 0, sin informe de fallo). Ahora reiniciar quita la pausa
  (`reset_game` en `app/main.c`). Comprobado además con la fachada que estéreo + pantalla ancha + «Original» + dos reinicios corre bien.

- Mudo tras «Reiniciar el juego» (con o sin estéreo; fallo anterior al mod): `ym2151_reset` no borraba los temporizadores ni el «ocupado
  hasta» de la interfaz de ymfm, que son instantes absolutos del reloj de la placa. Tras reiniciar el reloj vuelve a 0 y el temporizador A
  quedaba programado donde iba la partida anterior; el Z80 solo lee los comandos en esa interrupción, así que no sonaba nada. Ahora se
  borran (`timer`, `busy_end`, `irq`, `sync`). Medido con la fachada: moneda tras el reinicio RMS 0,01832 (antes 0), frente a 0,01833 en
  el arranque en frío; quedan 22 bytes de estado y diferencias de audio de como mucho 0,0036 respecto al arranque en frío, probablemente
  contadores internos del chip que `ymfm` no reinicia (también los conserva una placa real). Sin reinicios, salidas idénticas a la línea base.

## Pendiente

Seguir al objeto mientras suena la frase. Si se quiere un reinicio idéntico al arranque en frío, revisar qué conserva `chip.reset()` de ymfm.
