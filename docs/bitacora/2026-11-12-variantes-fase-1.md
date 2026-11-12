# 2026-11-12 · Variantes de SF2, fase 1: Champion Edition en vanilla, idéntica a MAME

## Pregunta de partida

Con el núcleo preparado (fase 0, D63) y las ROMs de CE en `sf2ce/` (18 archivos, CRC iguales al catálogo; carpeta fuera de
git), recompilar *Street Fighter II': Champion Edition* (World 920513) y dejarla idéntica a MAME 0.289, sin mods. Plan en
`docs/VARIANTES.md`.

## Qué se hizo

- **Orden de bytes.** `ROM_LOAD16_WORD_SWAP` invierte los dos bytes de cada palabra del archivo (la fase 0 suponía lo contrario: el PC
  de reset salía 0x3602 y el vector de la IRQ2, impar). Corregido en `romset.c`, `tools/games.py` y la prueba unitaria.
- **MAME acepta el set** pese a un volcado de PLD de otro tamaño (`ioc1.ic7`, 279 bytes en vez de 260): solo avisa y corre.
- **Análisis** (`tools/hints_sf2ce.txt`, compilación en `build-ce/` con `-DSF2_GAME=sf2ce -DSF2_ROM_DIR=<sf2ce>`):
  - `entry 0000c0`: el arranque acaba con `trap #14`, cuyo manejador pone 0xC0 como PC de retorno (arranque del sistema de tareas).
    De 802 instrucciones se pasa a 50.728.
  - 13 `table_end` (tablas de saltos .w sobreextendidas; dos son las gemelas de las 0x1D486/0x1D50C de WW) hasta 0 conflictos, 0 fallos
    de decodificación y 0 barridos solapados, como WW.
  - Planificador: `entry` 0x206, 0x20C, 0x21C (tabla de punteros largos con base PC-relativa que no se resuelve) y 0x1C4 (retorno por
    `pea` por debajo de 0x400, que el análisis no sigue).
  - Pista nueva `offset_table BASE N PASO` (`tools/analyze.py`): tabla intercalada desplazamiento/bandera de 0x3C9CC (32 destinos).
  - 0x19212: el juego indexa una tabla de 3 estados con d0 = 8 y salta a 0x19214, dentro de la tabla (también en MAME, cuadro 781 de
    `inputs_sf2.txt`): la tabla se queda con su primera palabra, sus destinos van con `offset_table` y 0x19214 es una entrada.
- **Código en RAM (D64).** CE monta dos rutinas en la RAM gráfica y las llama con direcciones ofuscadas: 0x6538 copia 0x51 bytes de
  0x85EB8 sumando 3 a cada uno en 0x910900 (llamada cada 4 cuadros desde 0x26FA con `0x244240 << 2`) y 0x666A copia 0x11 palabras de
  0x72536 sumando 0x18 en 0x918900 (llamada desde 0x397E con `0x123120 << 3`). Pista `ram_code DEST ORIGEN LON SUMA [w]`: el análisis
  decodifica esos bytes sobre una imagen ampliada, `gen_c.py` genera la función en su dirección de RAM y al entrar comprueba
  (`rt_check_ram_code`) que la memoria tiene esos bytes; si no, error fatal en vez de ejecutar otra cosa.
- **Datos del programa** (`game.c`): manejador de la IRQ2 0x5C6; no lee el CPS-B, así que el punto de muestra es su primer acceso de
  E/S, la escritura de 0x800100 en 0x5DA; contador de cuadros 0xFF801C y bandera de VBlank 0xFF020E, como WW. Los scripts de MAME
  toman el punto de muestra de `tools/mame_game.lua` (tap de lectura o de escritura según la variante, `SF2_GAME`), y
  `mame_audio.lua` / `compare_audio.py --game`, el reloj.
- **Reloj de la IRQ (D65).** El único cuadro distinto (el 2, solo el PC interrumpido) venía de la primera IRQ: MAME empieza el
  manejador 10 ciclos antes. Leyendo `state_interrupt` de `m68000-sdf.cpp`: 6 ciclos internos y una escritura antes del IACK, que
  empieza en fin + 10; `vpa_sync` lo lleva al primer múltiplo de 10 ≥ fin + 14 y el manejador empieza 35 ciclos después, o sea en el
  primer ciclo ≥ fin + **49** que es 5 mod 10. La regla medida en WW (fin + 50) solo difiere si el fin es 6 mod 10, cosa que en WW no
  ocurre en las IRQ (cuadro de 167.680 ciclos, múltiplo de 10); a 12 MHz (201.216) sí. Corregido en `irq_entry_cycle` (`runtime/cpu.h`).
- **Mods (D66).** En una variante sin mods la fachada rechaza la pantalla ancha (ancho distinto de 384), la lógica ancha, el stage
  completo, el entrenamiento y sus cajas, el estéreo y las herramientas de desarrollo: leen y escriben RAM y ROM de WW. `sf2_has_mods()`.
  `sf2_rom_tests` cambia las pruebas de mods por `test_no_mods` en esas variantes.
- `tools/CMakeLists.txt`: los objetivos `compare-*` lanzan `${SF2_GAME}` en MAME y le pasan `SF2_GAME`; `make compare-mame GAME=sf2ce
  BUILD=build-ce ROMDIR=sf2ce` funciona.

## Verificación

- CE, modo atracción, 20.000 cuadros: resumen por cuadro idéntico a MAME (RAM de trabajo, RAM gráfica, entradas, contador **y**
  registros: 0 cuadros distintos tras D65), 1.941 comandos de sonido iguales, 9 capturas idénticas píxel a píxel.
- CE con `tools/inputs_sf2.txt` (moneda, start, selección, combates contra Honda), 12.000 cuadros: idéntico a MAME, 1.713 comandos
  iguales, 6 capturas idénticas.
- CE, audio de 8.000 cuadros (`compare_audio.py --game sf2ce`): 298.250 escrituras y 209.899 lecturas del Z80 iguales al ciclo; YM2151,
  OKI, mezcla y WAV idénticos.
- CE, 100.000 cuadros de atracción sin fallos de despacho; `build-ce/sf2 sf2ce` arranca sin ventana; `ctest` 8/8 en `build-ce`
  (`sf2_rom_tests` 19.884 comprobaciones; paso cerrado con Musashi incluido).
- WW tras todos los cambios (análisis, generador, reloj de la IRQ): salida idéntica byte a byte a la línea base (resumen, WAV, comandos,
  capturas, traza de estado), `ctest` 8/8 y las 14 secuencias de `tools/sequences/` reproducen su CRC.

## Pendiente

- Secuencias propias de CE (`playgen.py` usa direcciones de RAM de WW y 8 personajes; CE tiene 12) y `make compare-coverage` para CE.
  La cobertura dinámica de las partidas probadas es ~32 %: el resto del juego (los cuatro jefes jugables, finales, más personajes)
  no se ha ejecutado todavía y puede tener más fallos de despacho.
- La aplicación sigue enseñando los menús de mods en CE (se rechazan al usarlos).
- Fase 2: Hyper Fighting.

## Prueba del usuario

Al jugar, CE se detuvo dos veces con un salto sin código en 0x4364A desde 0x43610 (cuadros 66.647 y 2.095 de dos partidas): la tabla de
0x43614 tiene 5 entradas y el análisis la cortaba en 2. Arreglado con `table_end 043614 04361e`. Buscando tablas terminadas antes de su
primer destino apareció otra de riesgo, 0x9551C, indexada con un byte con signo que puede leer las 5 palabras anteriores a su base: sus
destinos pasan a ser despachables (`label`). Sin las entradas del usuario no se pudo repetir su partida (el estado de rescate corre sin
fallos, pero puede no pasar por ahí); se comprobó que 0x4364A, 0x43688 y 0x436C4 están en la tabla de despacho, que la partida de
12.000 cuadros sigue idéntica a MAME y `ctest` 8/8. La aplicación decía «Sonido estéreo: activado» aunque CE lo rechaza; ahora avisa de
que no está disponible.
