# 2026-11-12 · Variantes de SF2, fase 2: Hyper Fighting en vanilla, idéntica a MAME

## Pregunta de partida

Con CE jugable (fase 1), añadir *Street Fighter II': Hyper Fighting* (set `sf2hf` de MAME, World 921209). El usuario puso sus ROMs en
`sf2hf/` (y `sf2hf.zip`). Mismo hardware que CE (`cps1_12MHz`, `CPS_B_21_DEF`, `mapper_S9263B`); solo cambian las tres ROMs de programa
(`s2te_23.8f`, `s2te_22.7f`, `s2te_21.6f`), con los CRC de MAME.

## Qué se hizo

- Catálogo `game_sf2hf` (`runtime/game.c`, `tools/games.py`), `sf2hf` en `SF2_GAME` y en `tools/mame_game.lua`; compilación en `build-hf/`.
  `sf2ce/` y `sf2hf/` entran en `.gitignore` (antes solo los zip).
- El programa está reorganizado respecto a CE (reset en 0xB91E4, IRQ2 en 0x1D1E0) pero tiene los mismos problemas, que se resolvieron
  con las mismas pistas en `tools/hints_sf2hf.txt`:
  - `entry 040378`: retorno del `trap #14` del arranque (la 0xC0 de CE). De 1.050 instrucciones a ~52.400.
  - Pista nueva `not_vector N` (`tools/analyze.py`): en HF, como en CE, 0xC0-0xFF es código y no los vectores 48-63, pero dos de sus
    palabras largas (vectores 51 y 56) caen en la ROM y se tomaban por manejadores.
  - 10 `table_end` (las mismas familias que en CE: tablas de 2 entradas con 0x7F38/0x7F14, el patrón 0x600/0x800/0xA00, la 0xA126E…).
  - Planificador: entradas 0xB91CA, 0xB9198 y 0xB91B4; tabla intercalada de 0x840E0 con `offset_table` (36 elementos, 4 más que CE).
  - Resultado: 0 conflictos, 0 fallos de decodificación, 0 barridos solapados.
- HF no tiene las rutinas de protección en RAM de CE (no aparecen ni los bucles de copia ni las llamadas ofuscadas, y el juego corre
  sin ellas).
- **Punto de muestra.** El manejador de la IRQ2 de HF escribe registros de vídeo antes de guardar los registros (`movem` en 0x1D24C):
  el punto de muestra es el primer acceso de E/S tras el `movem`, la escritura de 0x800122 en 0x1D264, para que el SR y el PC apilados
  estén en SP+60/62 como en las demás. Contador de cuadros 0xFF801C; la bandera de VBlank pasa a 0xFF04CE (zona volátil).
- **Reloj de la IRQ (D67).** La primera comparación dio 2 cuadros de atracción y 10 de la partida con la IRQ atendida una instrucción
  más tarde que en MAME (y uno con la RAM gráfica distinta, resincronizado al siguiente). Traza del cuadro 2.650: el VBlank llega en el
  ciclo 192 durante `lsr.w #3,d2` (185-197), cuyo único acceso es la precarga del principio; MAME atiende la IRQ tras el `add.w`
  siguiente y nosotros tras la instrucción de después. `execute_run` de MAME para la CPU también al acabar cada instrucción (`while
  (m_icount > 0)`), no solo tras los accesos al bus: la IRQ es visible desde el primer acceso **o fin de instrucción** que llega a su
  instante. Para una instrucción sin acceso antes de su latch (`add.w`) el punto de reconocimiento es su comienzo, no el último acceso de
  la anterior (`runtime/machine.c`). Solo cambia algo si la anterior acaba con ciclos internos sin acceso.

## Verificación

- HF, 100.000 cuadros de atracción sin fallos de despacho a la primera.
- HF contra MAME: 20.000 cuadros de atracción y 12.000 de `inputs_sf2.txt` idénticos (RAM, registros: 0 cuadros distintos), 1.951 y
  1.637 comandos de sonido iguales, 9 + 6 capturas idénticas; audio de 8.000 cuadros con entradas: Z80 al ciclo (310.884 escrituras y
  215.769 lecturas), YM2151, OKI y mezcla idénticos.
- Tras D67: WW idéntico byte a byte a la línea base y las 14 secuencias con su CRC; CE de nuevo sin diferencias en sus 20.000 + 12.000
  cuadros y comandos.
- `ctest` 8/8 en `build`, `build-ce` y `build-hf`.

## Pendiente

Como en CE: secuencias propias (12 personajes) y `compare-coverage`; cobertura dinámica de lo probado ~31 %. Fase 3: mods en CE y HF.
