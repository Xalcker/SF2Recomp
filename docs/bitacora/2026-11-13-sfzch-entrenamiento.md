# 2026-11-13 · Modo entrenamiento y visor de entradas en Street Fighter Zero (D74)

## Pregunta de partida

Tras el estéreo (D73), el siguiente paso más barato para sfzch: encontrar la RAM básica de la partida (estado, reloj, vidas) y con ella
llevar el visor de entradas y el modo entrenamiento. Las cajas de golpe quedan fuera (el formato de SFZ está sin descifrar).

## Cómo se encontró la RAM

- Script de `sf2py` (scratchpad, no está en el repositorio): una partida contra la CPU (start, selección, Ryu avanza y golpea a Rose) que
  guarda la RAM de trabajo de cada cuadro entre el 2.600 y el 5.200, con capturas.
- **Reloj.** Ningún byte BCD bajaba como el marcador; buscando bytes que solo bajan de 1 en 1: **0xFFAE09**, en binario (98 en pantalla
  en el cuadro 2.900 = 98; 87 en el 3.500), y su subcontador **0xFFAE0A**, de 0x3B a 0 por segundo.
- **Vida.** Palabras de la estructura de cada luchador que solo bajan: **+0x40** (0x90 llena, una bajada por golpe, negativa en el KO) y
  dos que la siguen bajando poco a poco, +0x42 y +0x14A (las barras).
- **Estado.** 0xFF8001 = 2 en toda la partida; 0xFF8005 = 2 en la selección y 4 desde el VS; en el combate 0xFF8009 = 4 y 0xFF800D vale 0
  en la presentación del asalto, 2 mientras se pelea y 4 en el KO (antes, 0xFF8009 = 2 con 0xFF800D = 0x0A/0x0C en el VS).
- X de cada luchador en +0x10 (la misma que usa el estéreo).

## Qué se hizo

- `TrainingRam` en el descriptor (`runtime/game.h`, `game.c`) con la RAM de WW, CE/HF y sfzch; `training.c` lo lee en vez de los valores
  fijos. Las cajas siguen con `ModRam` (solo variantes con todos los mods).
- `sf2_training_set`: el entrenamiento con `TrainingRam`; el visor de entradas en cualquier variante (`training_vblank` también registra
  las entradas cuando la variante no tiene entrenamiento). `sf2_has_training()` en la fachada.
- Versus automático sin monedas: con `credits` = 0 pulsa Start 1 y Start 2 directamente.
- **Barras.** Primera prueba: la vida se recargaba pero la barra de Ken seguía casi roja; las copias +0x42/+0x14A solo bajan. Al recargar
  se escriben también (`life_bar` del descriptor).
- `tests/rom_tests.c`: `test_training` lee el descriptor y se corre en toda variante con entrenamiento; `test_no_mods` acepta el
  entrenamiento donde existe.

## Verificación

- `test_training` en sfzch: el versus automático llega al combate (3.778 cuadros de pelea en 4.500), con el reloj en 99 y las dos vidas en
  0x90 en todos los cuadros de combate; el dummy sigue el guion y no recibe las teclas del anfitrión; con el entrenamiento apagado o solo
  el visor, la partida no cambia.
- `sf2py`: sin recarga, 17 golpes a J2 dejan su vida y las dos barras a 0; con recarga inmediata, las tres se quedan en 0x90.
- Capturas (aplicación, `--config` con el entrenamiento): versus automático Chun-Li contra Ken, reloj fijo en 99, barra de Ken que vuelve
  a llenarse.
- Regresión (Windows, MAME 0.289): `ctest` 8/8 en sf2, sf2ce, sf2hf y sfzch, con `test_training` también en las tres de SF2 leyendo su
  descriptor (3.456, 3.538 y 3.685 cuadros de combate en el versus automático); con los mods apagados, las cuatro idénticas a MAME en
  12.000 cuadros (estado, 17 capturas, comandos de sonido y audio).

## Pendiente

- Cajas de golpe de SFZ (formato de las tablas por descifrar).

Probado jugando (menú ▸ Entrenamiento, teclas 7 y 8): visto bueno del usuario.
