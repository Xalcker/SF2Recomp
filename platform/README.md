# platform/

Servicios del anfitrión que el núcleo (`runtime/`) no tiene que saber, sin SDL (decisión D37): lo usan igual el juego,
los arneses de validación, las herramientas y las pruebas. Es C estándar con stdio.

- `hostio.c/.h`: leer y escribir archivos (la escritura es atómica: `archivo.tmp` y luego renombrar), **cargar las ROMs
  desde un zip o una carpeta** (`hostio_load_romset`: el zip se detecta por su contenido y se abre con
  `runtime/zipread.c`; la carpeta se busca con los nombres de MAME, actuales o antiguos), leer la secuencia de entradas
  por cuadros (`cps1_load_inputs`) y escribir PNG.
- `wav.c/.h`: escritor WAV PCM de 16 bits (`wav_add_float` convierte como la grabación de MAME).
- `config.c/.h`: la configuración del juego (`sf2recomp.ini`): dipswitches con nombre, escala de ventana, volumen,
  remapeo de teclado y mando, teclas rápidas. Lee y escribe el texto, valida y avisa; no conoce SDL (los nombres de teclas
  son texto que resuelve `app/input.c`). `config_dsw` calcula los tres bytes de dipswitches (por omisión 0xFF/0xFC/0x9F, los
  de MAME).

Formato del archivo (se crea con los valores por omisión la primera vez, ver `docs/bitacora/2026-10-19-hito5-configuracion-y-estados.md`):

```ini
[video]   scale = 3   aspect = pixel | 4:3   fullscreen   integer_scale   smooth   vsync   hfr
          widescreen   wide_width = 512   hud = center | edges   wide_logic   stage_full        (mods: docs/MODS.md)
[audio]   enabled   volume = 0..100   rate = 48000
[dip]     difficulty = 0..7 (3 = normal)   coin_a / coin_b = 4c1c ... 1c6c   free_play   two_coins_to_start
          demo_sounds   allow_continue   freeze   test_mode
[training] enabled   infinite_time   refill = off | delay | always   refill_delay = 60   lock_p2   auto_vs
          dummy = stand | crouch | jump | block | block_crouch | walk_forward | walk_back | script   script = "d:30 df:3 f+lp:2 n:30"
          show_inputs   show_hitboxes                                                   (docs/ENTRENAMIENTO.md)
[paths]   rom = /ruta/sf2.zip   state_dir = ...
[keys]    p1_up = Up ...  (hasta cuatro nombres de tecla de SDL por acción, separados por comas)
[pad]     p1_lp = x ...   (botones de mando de SDL; lefttrigger / righttrigger valen como botones)
[hotkeys] save_state = F5   load_state = F7   slot_prev = F6   slot_next = F8   pause = P   fast_forward = Tab
          fullscreen = F11   screenshot = F12   reset = F10   quit = Escape   menu = M   widescreen = F9   wide_logic = F3
          stage_full = F4   hfr = F1   training = 7   show_inputs = 8   show_hitboxes = 9
```
