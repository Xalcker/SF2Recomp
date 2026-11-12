# 2026-11-12 · Variantes de SF2, fase 3: los mods en Champion Edition y Hyper Fighting

## Pregunta de partida

Llevar a CE y HF los mods de WW (pantalla ancha y HUD en los bordes, lógica ancha, stage completo, estéreo, entrenamiento con sus visores y
cajas de golpe, HFR), con la regla de siempre: apagados, el juego idéntico a MAME. Plan general en `docs/VARIANTES.md`.

## Inventario

Los mods dependen de 37 instrucciones parcheadas (`recompiler/mod_patches.txt`: paredes y cámara, ventanas de actividad y de visibilidad,
proyectiles, constructor de sprites, cola de sonido) y de ~15 direcciones de RAM (modo/fase/subfase, créditos, jugadores, temporizador,
cámara y sus topes, oponente/escenario, reserva de proyectiles, cola de sonido, rango de objetos). Las herramientas de desarrollo son
propias de WW y no se portan.

## Cómo se localizó todo

- **Código.** Cada instrucción parcheada de WW se buscó en CE y HF por el código que la rodea (ventanas de 8-48 bytes que aparecen una sola
  vez). 29 de 37 salieron solas; las demás a mano: tres comprobaciones del constructor de sprites (mismo patrón repetido) y las tres
  ventanas de los proyectiles (`move.w 0x3e(a6),d1 ; addi.w #$100,d1 ; cmpi.w #$200,d1`), que **no existen** en CE ni en HF: esa lógica se
  reescribió. CE y HF tienen además un **segundo constructor de sprites** (CE 0x96D7E…, HF 0x9EBF4…) con las mismas ocho comprobaciones.
- **RAM.** Para cada desplazamiento de a5 que usan los mods se tomaron las instrucciones de WW que lo usan, se localizaron en CE/HF y se
  leyó qué desplazamiento usan allí, por votación. CE y HF comparten la RAM: créditos 0x2D9 (WW 0x2DD), jugadores 0x3B8/0x6B8 (0x3C0/0x6C0),
  oponente 0x660 (0x668), temporizador 0xABE (0xACE), cámara 0xBC4/0xBC8 (0xBD8/0xBDC), topes 0x9C4/0x9C6 (0x9D0/0x9D2), cola de sonido
  0x19E (0x1A2), proyectiles 0x1376 (0x138A); modo/fase/subfase e índice de la cola, iguales. Se comprobó jugando (`sf2py`): modo 2 /
  fase 4 / subfase 0x0A en combate, vidas 0x90, temporizador BCD, X de los luchadores y cámara; la reserva de proyectiles, lanzando
  hadokens (aparecen sus cajas).

## Qué se hizo (D68)

- `ModRam` en el descriptor (`runtime/game.h`), con la de WW y la de CE/HF (`game.c`); `training.c`, `video.c` (escena ancha, escenario),
  `cpu.h` (`mod_sound_tag`) y `machine.c` (índice de la cola) la leen en vez de constantes. `has_dev_tools` separa las herramientas de
  desarrollo (solo WW).
- `recompiler/mod_patches_sf2ce.txt` y `mod_patches_sf2hf.txt` (39 parches: los 31 portados y los 8 del segundo constructor); CMake usa el de
  la variante. `gen_c.py` comprueba que cada parche encaja exactamente una vez en el C de su instrucción: los 39 encajan en las dos.
- `sf2_game_crc()`: CRC solo de la partida, sin la tabla de sprites latcheada del estado, que con pantalla ancha y lógica original presta
  la máquina sombra (D62). Las pruebas de pantalla ancha comparaban `sf2_state_crc` y en CE/HF fallaban por eso, no por la partida.
- `sf2py`: ancho de vídeo, lógica ancha, stage completo, estéreo y cajas, para probar los mods desde Python.

## Verificación

- Mods apagados: CE y HF idénticas a MAME en sus 20.000 cuadros de atracción y 12.000 de partida (también registros) y comandos de sonido.
- `sf2_rom_tests` (pantalla ancha, HUD, lógica ancha, stage completo, entrenamiento con dummy y versus automático) pasa en CE y HF; `ctest`
  8/8 en las tres variantes.
- Capturas: 16:9 en CE, stage completo a 768 px en CE (escenario de Zangief entero), lógica ancha en HF (Ryu luchando en la banda añadida).
- Estéreo: en las tres, ~18 % de las ventanas con sonido salen claramente de un lado.
- Cajas de golpe: en CE y HF, como en WW, siempre junto a su luchador y con tamaños razonables; con hadokens aparecen las de proyectil.
- WW: salida idéntica byte a byte a la línea base, estéreo idéntico al binario de antes de la fase 0, `sf2_rom_tests` y las 14 secuencias.

## Límites y pendiente

- **HF, pantalla ancha con la lógica original:** en ~0,7 % de los cuadros (20 de 3.000) el centro del cuadro ancho difiere del de 384 px en
  unas decenas de píxeles (el marcador del tiempo, algún sprite): la tabla de la máquina sombra llega sin algún elemento, porque sus ventanas
  ensanchadas cuestan ciclos y HF va más deprisa. Solo afecta al dibujo; la prueba lo tolera hasta un 1 %. En CE, 0 cuadros.
- **Proyectiles con lógica ancha o stage completo:** las ventanas de vida de WW (0x2D46E, 0x33218, 0x33296) no existen en CE/HF; no se ha
  medido dónde desaparecen allí los proyectiles con la vista ancha.
- Los arreglos de dibujo por escenario de la pantalla ancha (siluetas de Guile/Ken, zonas oscuras de Balrog/Zangief, elefantes de Dhalsim)
  usan códigos de tiles de WW; en CE/HF no se han revisado escenario por escenario.
- Herramientas de desarrollo: solo WW.
