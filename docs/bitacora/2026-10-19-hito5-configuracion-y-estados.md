# 2026-10-19 · Hito 5 · Configuración, dipswitches, controles, estados guardados, carga de ROMs y sincronía con el audio

## Pregunta de partida

Qué necesita un jugador que no es quien escribió el recompilador: indicar sus ROMs (en zip o sueltas) y que se le diga qué
falta, cambiar dipswitches y controles sin recompilar, guardar y cargar la partida, y que el juego corra a la velocidad correcta
con el audio de su máquina.

## Qué se hizo

### Carga de ROMs (D38)

`runtime/romset.c` lleva el catálogo de las 23 ROMs de `ROM_START( sf2 )` de MAME 0.289 (programa 8, gráficos 12, Z80 1, OKI
2) con nombre actual, nombre antiguo, CRC-32, tamaño y destino. El anfitrión da un `RomSource` (`get(i)` describe el archivo i,
`read(i)` lo lee); el cargador filtra por tamaño, identifica por CRC (el del directorio central si es un zip, calculado si es
una carpeta) y rechaza lo que no coincide. Una carpeta con los nombres antiguos de `sf2/` y el `sf2.zip` con los actuales dan
exactamente los mismos bytes (probado ROM por ROM) y la misma imagen de programa (SHA-1 `6bb03cf7…`, CRC `43fcc23e`).

El zip lo abre `runtime/zipread.c` (directorio central, «stored» y «deflate», inflate propio): todos los desplazamientos se validan
(el archivo no es de fiar), el CRC del directorio central se comprueba al extraer, y se probó con tres vectores de zlib (dinámico,
fijo, stored), flujos cortados, salidas demasiado cortas, 2.000 entradas basura y zips corruptos. Mensajes (los que ve el
usuario, en la consola, en un cuadro de SDL o en la página):

```
Faltan 1 de las 23 ROMs de Street Fighter II (World 910522, set "sf2" de MAME):
  - sf2_9.12a (o sf2_09.bin), 64 KiB, CRC a4823a1b -> el archivo "sf2_9.12a" tiene el CRC 0c39a6ee: no es esta revision o esta danado
```

(Probado en el navegador con un `sf2.zip` al que se cambió un bit; otros casos: zip sin ROMs, archivo que no es un zip, carpeta
vacía y ROMs que no son las del código recompilado, que se detecta por el CRC de la imagen.)

### Configuración (D42)

`platform/config.c`, archivo `sf2recomp.ini` (secciones `[video] [audio] [dip] [paths] [keys] [pad] [hotkeys]`). Se crea con todos
los valores y comentarios la primera vez que arranca el juego (`--write-config` lo fuerza); los valores no válidos avisan por
línea y se conserva el anterior. Está en `<preferencias de SDL>/SF2Recomp/sf2recomp.ini` (en macOS
`~/Library/Application Support/SF2Recomp/sf2recomp/`), o donde diga `--config`; en el navegador, en IndexedDB.

- **Dipswitches con nombre**, calcados de `INPUT_PORTS_START( sf2 )` y `CPS1_COINAGE_1`/`CPS1_DIFFICULTY_1` de MAME:

| Clave | Valores | Bits |
|---|---|---|
| `difficulty` | 0 (fácil) a 7; 3 = normal | DSWB 0-2 = 7 − n |
| `coin_a`, `coin_b` | `4c1c 3c1c 2c1c 1c1c 1c2c 1c3c 1c4c 1c6c` | DSWA 0-2 y 3-5 (`1c1c` = 7 = 0x07) |
| `two_coins_to_start` | false por omisión | DSWA 6 (activo a 0) |
| `free_play` | false | DSWC 2 (activo a 0) |
| `freeze` | false | DSWC 3 (activo a 0) |
| `demo_sounds` | true | DSWC 5 (activo a 0) |
| `allow_continue` | true | DSWC 6 (activo a 0) |
| `test_mode` | false | DSWC 7 (0 = menú de pruebas) |

  Los valores por omisión dan 0xFF/0xFC/0x9F, los de MAME (`unit` lo comprueba). `flip_screen` no se ofrece: el vídeo no lo
  implementa (D24). Comprobado de punta a punta: con `free_play = true` la pantalla de atracción dice «FREE PLAY» y con la
  configuración por omisión «INSERT COIN» (cuadro 900 de dos ejecuciones sin ventana con `--png-frames`; las capturas, que son
  imágenes del juego, no se guardan en el proyecto).
- **Vídeo**: `scale` (0 = ajustar), `aspect` (`pixel` o `4:3`, la proporción del monitor de la recreativa), `integer_scale`,
  `smooth`, `fullscreen`, `vsync`.
- **Audio**: `enabled`, `volume` (ganancia del flujo de SDL, nunca sobre la mezcla del núcleo), `rate`.

### Teclado y mandos (D42)

`app/input.c`: cada acción de juego (monedas, start, servicio, 4 direcciones y 6 botones por jugador, 25 en total) tiene hasta
cuatro teclas (nombres de SDL: `Up`, `Left Shift`, `Keypad 4`, `F2`…) y cuatro botones de mando (`a`, `dpup`, `rightshoulder`,
`lefttrigger`…); mando 1 = jugador 1, mando 2 = jugador 2 en el orden de conexión, la cruceta y el stick izquierdo mueven
siempre, los gatillos cuentan como botones. `input_check_config` comprueba que todos los nombres existen en SDL y la prueba
`input_mapping` lo exige para los valores por omisión. Esa prueba usa **mandos virtuales de SDL3** (`SDL_AttachVirtualJoystick`)
para pulsar botones y ejes y comprobar las tres palabras de entrada: X/A/izquierda/Start del mando 1, stick arriba y gatillo
derecho, mando 2 independiente y una asignación cambiada en la configuración. (El teclado solo se prueba por nombres: SDL no
permite inyectar teclas.)

### Estados guardados (D40)

`F5` guarda y `F7` carga la ranura actual (0-9, `F6/F8` cambian), en `<preferencias>/states/sf2_slotN.sta` (en el navegador,
IndexedDB). Ver `runtime/README.md` para el formato. Propiedades probadas (`sf2_rom_tests`, también en wasm y en Wine):

1. Otra máquina recién creada que carga el estado sigue 360 cuadros con el mismo CRC de vídeo y de audio en cada cuadro.
2. La máquina original rebobina y repite lo mismo.
3. Un estado guardado después de cargar sirve igual (encadenado).
4. Estados rechazados sin tocar la máquina (el estado completo antes y después es byte a byte igual): truncado, un byte cambiado
   (CRC), marca equivocada, otra versión, otro código recompilado, bandera de la CPU fuera de rango (con el CRC recalculado) y PC
   que no está en la tabla de despacho (idem).
5. Se puede guardar en cualquier punto de 4.000 cuadros (arranque, aviso, atracción, selección, combate).
6. El contenido es idéntico entre plataformas: el CRC-32 del estado de la misma partida coincide en macOS, Linux, wasm y Windows
   (D45), es decir, un archivo de estado guardado en una plataforma carga en las demás.

### Ritmo y frecuencia de salida (D41, resuelve el pendiente de D35)

`app/main.c` emula un cuadro más mientras la cola de audio tiene menos de 60 ms (hasta 4 por vuelta); sin audio, reloj de 59,637 Hz;
con `Tab` o `--nothrottle`, sin esperas. El dispositivo se abre a `[audio] rate` y un `SDL_AudioStream` convierte la mezcla de
48 kHz. Con el CoreAudio real de este Mac, 10 s de partida: cola entre 17 y 78 ms, **0 huecos** (`--stats`), 59,0 cuadros por
segundo medidos. Con el controlador `disk` de SDL y `rate = 44100` y `96000`, el archivo de salida declara `F32LE channels=2
freq=44100` y `freq=96000` y contiene los mismos 16,81 s de audio: la frecuencia del dispositivo es la configurada y la mezcla
validada no cambia (D33 es el criterio para comparar otras frecuencias). En el navegador (ScriptProcessor de SDL) el título de la
página marcó entre 54 y 61 cuadros por segundo con el audio activo (con el sistema bajo presión de memoria bajó a esa cota).

## Comandos

```bash
build/sf2 /ruta/sf2.zip                                      # jugar; crea sf2recomp.ini la primera vez
build/sf2 sf2.zip --config /tmp/x.ini --write-config         # escribir la configuración actual
build/sf2 sf2.zip --load-state estado.sta --stats
ctest --test-dir build -R "unit|roms_and_states|input_mapping"
```

## Pendiente

- Probar el teclado y los mandos reales en las demás plataformas; en macOS se probó el teclado a mano solo en el navegador
  (monedas, start, guardado de estado) y los mandos con dispositivos virtuales.
- Los avisos de pantalla usan la fuente de depuración de SDL (8x8); es suficiente y no añade dependencias.
