# app/

El juego: **`sf2`**, el único ejecutable que habla con SDL3 (ventana, teclado y mandos, audio) y con el sistema de
archivos para las preferencias. Sustituye al visor `sf2_video` de los hitos 3 y 4. Es el mismo código en macOS, Linux,
Windows y el navegador (Emscripten).

```
sf2 [sf2.zip|carpeta] [opciones]
  --config ARCHIVO   archivo de configuración (por omisión sf2recomp.ini en la carpeta de preferencias del usuario)
  --rom RUTA         sf2.zip o carpeta con las ROMs (también como primer argumento; si no se da, se busca sf2.zip
                     en [paths] rom, en SF2_ROM, en el directorio actual, junto al ejecutable y en preferencias)
  --scale N  --fullscreen  --volume 0-100  --mute  --nothrottle
  --inputs ARCHIVO   secuencia de entradas por cuadros (pruebas, make run-video)     --frames N   termina tras N cuadros
  --load-state ARCH  carga un estado guardado al arrancar
  --png-frames a,b   guarda esos cuadros como PNG en --png-dir (el mismo cuadro que MAME en tools/mame_snap.lua)
  --bench N [--bench-mode core|video|full]   mide cuadros por segundo y termina (ver el rendimiento en la bitácora)
  --stats            al terminar: ocupación de la cola de audio y cuadros emulados por vuelta del bucle
  --write-config     escribe la configuración con los valores actuales y termina
```

Teclas por omisión (todo se cambia en `sf2recomp.ini`, ver `platform/README.md`): flechas, **Z X C** puñetazos, **A S D**
patadas, **5** moneda, **1** start (jugador 2: IJKL, R T Y, F G H, 6, 2); **F5/F7** guardar y cargar estado, **F6/F8**
ranura anterior/siguiente (0 a 9), **P** pausa, **Tab** avance rápido (mantener), **F10** reiniciar, **F11** o Alt+Intro
pantalla completa, **F9** pantalla ancha, **F12** captura, **Esc** salir (`--widescreen` o `widescreen = true` la dejan activada; `--hud-edges` o `hud = edges` pegan el marcador a los bordes; `--wide-logic`, `wide_logic = true` o **F3** dejan que los luchadores usen todo el ancho (cambia el juego);
`aspect = 4:3` con `wide_width = 512` da una ventana 16:9 sin estirar). Mandos (SDL): cruceta o stick, X/Y/R1 puñetazos, A/B/R2 patadas, Back moneda,
Start start; el mando 1 es el jugador 1 y el 2 el jugador 2.

## Cómo funciona

- `main.c` usa los callbacks de aplicación de SDL3 (`SDL_AppInit/Event/Iterate/Quit`): en escritorio es el bucle de
  siempre; en el navegador, `requestAnimationFrame`. Cada vuelta emula cuadros con `sf2_run_frame` y presenta el último.
- **Ritmo** (D41): con audio, mientras la cola del flujo de audio tenga menos de 60 ms se emula otro cuadro (hasta 4 por
  vuelta); así el juego va a la velocidad exacta de la tarjeta de sonido. Sin audio, un reloj de 59,637 Hz. El
  dispositivo se abre a `[audio] rate` y un `SDL_AudioStream` convierte los 48 kHz de la mezcla validada.
- **Vídeo**: textura `XRGB8888` de 384 x 224, vecino más cercano o lineal, escala entera o libre, píxeles cuadrados o
  4:3; los avisos (estado guardado, errores) salen con `SDL_RenderDebugText`.
- **Estados** (D40): `sf2_slotN.sta` en `<preferencias>/states` (en el navegador, IndexedDB), ranuras 0 a 9; las carpetas `states` y
  `screenshots` se crean al guardar por primera vez. Con `--config archivo` el perfil es la carpeta de ese archivo (los estados y
  las capturas van a su lado y no se toca la carpeta de preferencias del usuario).
- `input.c/.h`: teclado (estado de teclado de SDL por nombre de tecla) y mandos (`SDL_Gamepad`, hasta dos) hacia las tres
  palabras de entrada de la CPS1; `input_check_config` valida todos los nombres.
- `web/shell.html`: la página del navegador (ver `docs/bitacora/2026-10-19-hito5-web.md`).

## Construcción

Ver `README.md` de la raíz (macOS, Linux, Windows y Web). `find_package(SDL3)` en escritorio, `--use-port=sdl3` en
Emscripten.

## Fallos del código recompilado (hito 6, D46)

Si el núcleo se detiene (por ejemplo, el juego salta a una dirección que el recompilador no vio), el juego lo dice en la
pantalla y por `stderr`, y añade una entrada a `errores.log` en la carpeta del perfil (la de preferencias, o la del archivo de
`--config`) con la fecha, el mensaje, el destino y la instrucción de origen del salto y el remedio (`entry XXXXXX` en
`tools/hints.txt`). Además guarda como `estados/fallo_cuadroN.sta` el último estado bueno (el juego copia el estado en memoria
cada 600 cuadros), que se carga con `--load-state` para reproducir el fallo. Con `--frames` (partidas guiadas, pruebas) el
proceso termina con error en vez de quedarse esperando. `--debug-jump N:DIR` provoca un salto a DIR en el cuadro N (prueba
`game_dispatch_failure`).

