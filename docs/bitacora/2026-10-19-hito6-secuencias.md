# 2026-10-19 · Hito 6 · Secuencias de entradas que recorren todo el juego

## Pregunta de partida

Hasta el hito 5 la única partida de validación era `tools/inputs_sf2.txt`: 12.000 cuadros con una moneda, Ryu perdiendo
contra Blanka, el fin de partida y la atracción (31,6 % de las instrucciones). El hito pide recorrer el juego entero: los
ocho personajes hasta el final (cuatro jefes y escena final incluidos), las fases de bonus, continuar y fin de partida, dos
jugadores, el menú de servicio y dipswitches distintos, en un formato documentado que lean igual el núcleo y MAME. Ganar
a la máquina de SF2 a ciegas, con entradas escritas a mano, no es realista; hacía falta un jugador.

## Qué se hizo

### Lo que hay que saber de la RAM (encontrado a mano, con volcados y capturas)

Con `build/libsf2py.dylib` (objetivo nuevo `sf2py`: la fachada `sf2.h` como biblioteca compartida para `ctypes`, con acceso a la
RAM de trabajo y a los estados en memoria) se jugó la partida de validación guardando la RAM de cada cuadro, y se buscaron los
bytes que cambian cuando debían:

| Dirección | Qué es | Cómo se encontró |
|---|---|---|
| 0xFF8001 | modo principal: 0x04-0x10 arranque y atracción, 0x04 título con créditos, 0x02 partida (y demo) | constante en cada pantalla |
| 0xFF8005 | fase de la partida: 0 selección, 4 combate, 6 final y créditos, 8 fin de combate perdido / CONTINUE?, 0x0A fin de partida, 0x0C selección bloqueada (tras continuar o con retador) | ídem |
| 0xFF8009 | sub-fase del combate: 0x08 presentación del asalto, 0x0A en juego, 0x0C KO, 0x0E-0x12 fin del asalto | ídem |
| 0xFF83C0 / 0xFF86C0 | estructuras del jugador 1 / 2 (paso 0x300) | |
| +0x06 | el jugador existe (0 en las fases de bonus: no hay rival) | comparando bonus y combates |
| +0x0C, +0x10 | x, y | |
| +0x30 | vida (0x90 llena, −1 tras el KO); +0x32 la barra que se dibuja | palabras que valen 0x90 y bajan con los golpes |
| +0x297 | cursor de la pantalla de selección = personaje (0 Ryu, 1 E. Honda, 2 Blanka, 3 Guile, 4 Ken, 5 Chun-Li, 6 Zangief, 7 Dhalsim, 8 M. Bison, 9 Sagat, 10 Balrog, 11 Vega) | moviendo el cursor; los rivales, comparando combates |
| +0x2A0 | puntuación (BCD, 4 bytes) | bytes que cambian solo cuando el rival recibe un golpe |
| 0xFF8518 | puntos de la fase de bonus (BCD) | buscando 0x1200 en la captura del coche |
| 0xFF82DD | créditos | (hito 2) |

El menú de pruebas no se abre con el interruptor de servicio sostenido (0xFF8076 bit 6, que solo enseña la versión al
arrancar) sino con el dipswitch «Game Mode = Test» (DSWC bit 7 a 0), que el arranque mira en 0x64DC y que crea la tarea del
menú en 0x84FBC.

### El jugador: `tools/playgen.py`

Juega sobre el núcleo recompilado sin vídeo ni sonido (unos 10.000 cuadros por segundo; guardar un estado cuesta 1,7 ms y
cargarlo 3,4 ms) y graba lo que pulsa cuadro a cuadro. Las entradas se fijan antes de cada cuadro con `sf2_set_input`, que el
núcleo aplica al entregar la IRQ2, exactamente como una línea de una secuencia; si una IRQ enmascarada junta dos cuadros, se
graban los dos. Por eso lo grabado se reproduce igual en `sf2_blind` y en MAME.

- **Combates**: búsqueda codiciosa con estados guardados. En cada decisión prueba todas las maniobras del repertorio del
  personaje (normales de pie, agachado y hacia delante, saltos con ataque, agarres, bloqueo, y las especiales: bolas de fuego
  y shoryuken de Ryu y Ken, cargas de Guile, Blanka, E. Honda y Chun-Li, golpes repetidos, lariat y piledriver de Zangief,
  yoga fire/flame), cada una seguida de 36 cuadros de bloqueo, y se queda con la que deja mejor `daño hecho − 1,15 × daño
  recibido + puntos/100` (±300 por un KO). Si pierde un asalto, vuelve a su principio y lo repite con otra semilla (hasta 4
  veces, 8 en dificultad máxima).
- **Fases de bonus** (no hay jugador 2): la misma búsqueda hacia los dos lados, maximizando los puntos de bonus.
- **Pantallas**: monedas, start cuando el título (modo 4) o, con juego gratis, la atracción (modo 2) lo aceptan; cursor del
  jugador 1 (y 2) hasta el personaje pedido; en CONTINUE? moneda y start cuando aparece «PUSH START»; nada en el resto
  (presentaciones, final, créditos).
- **Modos**: `arcade` (partida completa), `--lose-after N` (tras N combates se queda quieto y pierde), `--continues N`,
  `versus` (2 créditos y start del jugador 2), `challenger` (el jugador 2 entra a mitad de un combate), `service` (guion del
  menú de pruebas). El jugador 2 humano es una política determinista: una acción por tramo de 24 cuadros elegida por un hash
  del cuadro, de modo que las simulaciones de la búsqueda del jugador 1 ven lo mismo que pasará.
- **Comprobación**: `playgen.py --verify` reproduce cada secuencia con la biblioteca, compara el CRC del estado final con el
  `#@ state_crc` grabado y anota los hitos (`#@ mark`: rivales de cada asalto, bonus, final, continuar...) y los cuadros que
  conviene comparar en vídeo (`#@ snap`). Con `--resume` se sigue una partida desde un `.partial` (los puntos de control se
  escriben cada 3.000 cuadros y al fallar).

### Formato

`tools/sequences/README.md`: las líneas `desde hasta tecla` de siempre y directivas en comentarios (`#@ frames`, `#@ dips`,
`#@ state_crc`, `#@ mark`, `#@ snap`), que el lector del núcleo y los scripts Lua antiguos ignoran.

## Resultados

Las 14 secuencias de `tools/sequences/` (603.050 cuadros, 2 h 48 min de máquina, 493 KB de texto):

| Secuencia | Cuadros | Dipswitches | Qué recorre |
|---|---:|---|---|
| `ryu` | 60.915 | ff fc 9f (MAME) | 11 combates (7 rivales + Balrog, Vega, Sagat, M. Bison), 3 bonus, final y créditos, atracción |
| `ehonda` | 60.505 | ff **ff** 9f (dificultad mínima) | ídem |
| `blanka` | 64.964 | ff fc 9f | ídem |
| `guile` | 70.817 | **fa** fc 9f (2 monedas 1 crédito) | ídem |
| `ken` | 61.513 | ff **f8** 9f (dificultad máxima) | ídem |
| `chunli` | 67.957 | ff fc **9b** (juego gratis) | ídem |
| `zangief` | 65.658 | **bf** fc 9f (2 monedas para empezar, 1 para continuar) | ídem |
| `dhalsim` | 64.012 | ff **fe bf** (dificultad 2, demo sin sonido) | ídem |
| `continuar` | 20.997 | ff fc 9f | 2 combates ganados, derrota, CONTINUE? con moneda, revancha perdida, cuenta atrás, iniciales en la tabla de récords, fin de partida |
| `sin_continuar` | 12.124 | ff fc **df** (Allow Continue = No) | 1 combate ganado, derrota, fin de partida sin CONTINUE?, iniciales |
| `versus` | 16.119 | ff fc 9f | dos jugadores desde el principio (Ryu contra Ken), CONTINUE? del perdedor, el ganador sigue contra la máquina |
| `retador` | 16.000 | ff fc 9f | Chun-Li sola; en el 2.º combate entra el jugador 2 («Here comes a new challenger»), elige a Guile y luchan |
| `servicio` | 9.469 | ff fc **1f** (modo de pruebas) | menú: color, dipswitches, E/S (entradas, salidas, sonido), retícula, caracteres (guion en lazo cerrado sobre el cursor del menú) |
| `validacion` | 12.000 | ff fc 9f | la de los hitos 2-5 (`tools/inputs_sf2.txt` con las directivas) |

- Los ocho personajes ganan los 22 asaltos y las 3 fases de bonus sin perder ninguno (ni repetir: los reintentos no hicieron
  falta), Ken también en dificultad máxima. Cada partida tarda entre 9 y 17 minutos en generarse (las 13 a la vez en 8 núcleos).
- Las secuencias de esta tabla son la segunda generación: la primera se grabó con el reloj de Musashi y 6 de las 14 divergían
  de MAME (ver la entrada del reloj); con el reloj exacto se regeneraron todas con las mismas órdenes y semillas, y las 14 son
  idénticas a MAME cuadro a cuadro (entrada de cobertura).
- Las 14 reproducen su `#@ state_crc` con `--verify` (la que no lo anota es la de validación, escrita a mano).
- SF2 World Warrior no tiene dipswitch de vidas: las variantes de dipswitches son monedas, dificultad, juego gratis, continuar,
  sonido de la demo y modo de pruebas. «Flip Screen» no se usa porque el vídeo no lo implementa (D24).
- Tras continuar, y cuando entra un retador, el juego enseña «PLAYER SELECT» con el mapa (rivales tachados) en la fase 0x0C,
  pero quien ya jugaba no puede cambiar de personaje (es así en World Warrior; lo apuntó el usuario durante el trabajo y lo
  confirman las capturas): solo elige el jugador que entra. El generador solo mueve ese cursor.
- La tabla de récords con entrada de iniciales aparece en el fin de partida (fase 0x0A), no tras el final del personaje (fase
  6: escenas del final, créditos con viñetas de combates y «PRESENTED BY CAPCOM», y vuelta a la atracción). En `continuar` y
  `sin_continuar` el generador escribe tres letras; en la primera versión las dejaba correr y el juego ponía «A».
- Lo que no recorren: las herramientas de desarrollo ocultas (0x87ABA-0x8A2C8, que se activan con una bandera de RAM que el
  juego no pone nunca: no son alcanzables con entradas) y el volteo de pantalla.

### Tropiezos

- El primer generador daba por empezada la partida cuando el modo pasaba por 0: la atracción también pasa por 0 tras el aviso.
  Ahora solo cuenta el paso a 0 desde el título (2 o 4) justo después de pulsar start.
- `0xFF8062` parecía la bandera del bonus y no lo era (cambia también en combates); la buena es la estructura vacía del
  jugador 2.
- En CONTINUE? el start pulsado junto con la moneda se pierde: hay que esperar al «PUSH START».
- El primer guion del menú de pruebas iba a ciegas (pulsaciones en cuadros fijos) y se desincronizaba: el submenú de E/S
  vuelve el cursor al principio y la salida de OUTPUT regresa al menú principal. Ahora es un lazo cerrado: lee el modo del
  menú (0xFF8005, 0xFF800D) y pulsa hasta que el cursor llega a la opción.
- Generar las partidas encontró cuatro fallos del análisis estático (la primera vez, un «salto a dirección no decodificada»
  en el décimo combate de Ryu); van en la entrada de correcciones.

## Comandos

```bash
cmake --build build --target sf2py                      # build/libsf2py.dylib
python3 tools/playgen.py --char ken --dips ff,f8,9f --retries 8 --seed 11 --out tools/sequences/ken.txt
python3 tools/playgen.py --char guile --lose-after 2 --continues 1 --seed 23 --out tools/sequences/continuar.txt
python3 tools/playgen.py --mode versus --char ryu --p2-char ken --max-frames 16000 --seed 23 --out tools/sequences/versus.txt
python3 tools/playgen.py --mode challenger --char chunli --p2-char guile --challenge-at 1 --max-frames 16000 --seed 23 --out tools/sequences/retador.txt
python3 tools/playgen.py --mode service --dips ff,fc,1f --out tools/sequences/servicio.txt
python3 tools/playgen.py --verify tools/sequences/*.txt
```

## Pendiente

- `libsf2py` y `playgen.py` solo se han probado en macOS (en Linux x86-64 la biblioteca compartida necesitaría compilar el
  núcleo con `-fPIC`).
- Las secuencias dependen del código del juego, no del recompilador: si cambiara el reloj del 68000 (ver la entrada del reloj)
  la partida grabada sigue siendo válida mientras el juego reaccione igual a las mismas entradas en los mismos cuadros, y
  `--verify` lo dice.
