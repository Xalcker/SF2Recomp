# tools/sequences/ — secuencias de entradas que recorren el juego (hito 6)

Cada archivo `.txt` es una partida completa y reproducible: lo que pulsan los jugadores, cuadro a cuadro, desde el
encendido. Las corren igual el núcleo recompilado (`build/sf2_blind --inputs`, el juego `build/sf2 --inputs`) y MAME
(`tools/mame_blind.lua`), y `make coverage` / `make compare-coverage` las corren todas (`tools/coverage.py`).

## Formato

Es el formato de `tools/inputs_sf2.txt` (hito 2) más unas directivas en comentarios, que los lectores antiguos ignoran:

```
# Título de la secuencia (primera línea de comentario)
#@ frames 81234            ← obligatoria: cuadros que dura la partida (cuadro N = N-ésima entrega de la IRQ2)
#@ dips ff fc 9f           ← dipswitches DSWA DSWB DSWC en hexadecimal (por omisión los de MAME: ff fc 9f)
#@ state_crc 1a2b3c4d      ← CRC-32 del estado de la máquina al terminar (lo anota tools/playgen.py)
#@ mark 1555 asalto: Ryu contra Blanka (combates ganados 0)     ← hitos (informativos; los anota playgen --verify)
#@ snap 1585,2215,...       ← cuadros de vídeo que se comparan con MAME además de uno cada 2.000
600 605 coin1              ← desde hasta tecla (cuadros inclusive)
1000 1005 start1
...
```

- Teclas: `coin1 coin2 service start1 start2 test`, `p1_up p1_down p1_left p1_right p1_lp p1_mp p1_hp p1_lk p1_mk p1_hk`
  y las mismas `p2_*` (los nombres de `runtime/cps1.c` y de los scripts Lua). El cuadro N es la N-ésima entrega de la
  IRQ2 (el cuadro 1 llega al terminar el test de RAM, ~1,8 s); las entradas de N se aplican al entregar su IRQ2 (D18).
- Dipswitches (`INPUT_PORTS_START( sf2 )` de MAME, activos a nivel bajo): DSWA bits 0-2 monedas A (7 = 1 moneda 1
  crédito, 2 = 2 monedas 1 crédito…), bits 3-5 monedas B, bit 6 a 0 = «2 monedas para empezar, 1 para continuar»;
  DSWB bits 0-2 dificultad (7 = la más fácil, 4 = normal, 0 = la más difícil); DSWC bit 2 a 0 = juego gratis, bit 3
  congelar, bit 4 voltear la pantalla (no implementado, D24: ninguna secuencia lo usa), bit 5 a 0 = sonido en la
  demostración, bit 6 a 0 = se permite continuar, bit 7 a 0 = modo de pruebas. SF2 World no tiene dipswitch de vidas.
  En MAME los aplica `tools/mame_blind.lua` (`SF2_DIPS`) con una pasada previa que los deja en un cfg propio, para que
  valgan desde el encendido como en la placa.

## Las secuencias

| Archivo | Qué recorre | Dipswitches |
|---|---|---|
| `validacion.txt` | la partida de los hitos 2-5 (`tools/inputs_sf2.txt`): atracción, moneda, Ryu contra Blanka, continuar sin moneda, fin de partida, demo | de MAME |
| `ryu.txt` … `dhalsim.txt` | partida completa con cada uno de los ocho personajes: los 11 combates (incluidos los cuatro jefes), las tres fases de bonus, el final del personaje, los créditos y la vuelta a la atracción | uno distinto en cada una (ver la cabecera) |
| `continuar.txt` | gana dos combates, pierde, continúa (moneda y «PUSH START»), vuelve a perder y deja correr la cuenta atrás hasta el fin de partida. Tras continuar aparece «PLAYER SELECT» con el mapa, pero en SF2 World Warrior el personaje no se puede cambiar (fase 0x0C): el cursor queda fijo | de MAME |
| `sin_continuar.txt` | pierde con «Allow Continue» en No: fin de partida sin la pantalla de continuar | DSWC df |
| `versus.txt` | dos jugadores desde el principio (2 créditos, start del jugador 2): selección de los dos, combate entre ellos | de MAME |
| `retador.txt` | partida de un jugador con Chun-Li; a mitad del segundo combate entra el jugador 2 («Here comes a new challenger»), elige a Guile (solo el que entra elige; el otro conserva su personaje) y luchan | de MAME |
| `servicio.txt` | menú de pruebas: color, dipswitches, E/S (entradas, salidas, sonido), retícula y caracteres | DSWC 1f |

## Cómo se generan

`tools/playgen.py` juega sobre el núcleo recompilado a través de `build/libsf2py.dylib` (ctypes) y graba lo que pulsa.
En los combates prueba cada maniobra del repertorio del personaje desde un estado guardado, deja correr la partida un
horizonte fijo y se queda con la que más daño hace y menos recibe; si pierde un asalto vuelve a su principio y lo repite
con otra semilla (`--retries`). Fuera de los combates sigue las pantallas por las variables del juego (documentadas en la
cabecera de `playgen.py`). Como el núcleo da la misma partida que MAME (hitos 2-4), lo grabado se reproduce igual allí.

```bash
python3 tools/playgen.py --char ken --dips ff,f8,9f --out tools/sequences/ken.txt          # partida completa
python3 tools/playgen.py --mode versus --char ryu --p2-char ken --coins 1 --out tools/sequences/versus.txt
python3 tools/playgen.py --char guile --lose-after 2 --continues 1 --out tools/sequences/continuar.txt
python3 tools/playgen.py --mode service --dips ff,fc,1f --out tools/sequences/servicio.txt
python3 tools/playgen.py --verify tools/sequences/*.txt      # reproduce, comprueba #@ state_crc y anota hitos
```

`playgen.py` necesita `build/libsf2py.dylib` (objetivo `sf2py` de CMake; probado en macOS). Las secuencias no contienen
nada de las ROMs: solo números de cuadro y nombres de teclas.
