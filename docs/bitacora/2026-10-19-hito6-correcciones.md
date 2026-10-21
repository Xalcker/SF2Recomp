# 2026-10-19 · Hito 6 · Correcciones: análisis estático, red de seguridad y registro de fallos de despacho

## Pregunta de partida

Qué falla cuando el código recompilado recorre el juego entero sin la red de seguridad de Musashi, por qué, y cómo se
corrige en el sitio que toca (análisis, heurísticas, `tools/hints.txt`, máquina) sin perder nada de lo validado. Y cómo
hacer que un fallo así, en el ejecutable final, nunca sea silencioso.

## Qué se hizo

### La red de seguridad pasa a ser opcional y el despacho tiene un único punto

- `dispatch_fn` (código generado) llama ahora a `rt_dispatch`, que busca en la tabla, avisa a un gancho opcional
  (`Machine.on_dispatch`, para la cobertura) y, si la dirección no está, llama a `rt_unknown`. `Machine` guarda la última
  instrucción ejecutada (`last_pc`) y el último salto fallido (`miss_from`, `miss_to`). El mensaje dice destino, origen y
  remedio: «el juego llegó a código que el recompilador no vio: salto a 094c9e desde la instrucción 000ab6 (añadir 'entry
  094c9e' a tools/hints.txt y regenerar)».
- `sf2_blind` ya no usa Musashi salvo con `--fallback` (D46): sin red, una dirección desconocida detiene la partida con el
  mismo mensaje que el juego, que es lo que hace falta para medir la cobertura de verdad.
- Fachada: `sf2_dispatch_failure()` da origen y destino del fallo; `sf2_debug_jump()` (solo pruebas) hace saltar al 68000 a
  una dirección cualquiera para provocar uno.
- Juego (`app/main.c`): cada 600 cuadros guarda en memoria una copia del estado; si el núcleo se detiene escribe el motivo en
  `stderr`, en la pantalla y en `errores.log` (carpeta del perfil) con fecha, destino, origen y remedio, y guarda la copia como
  `fallo_cuadroN.sta`, que se carga con `--load-state` para reproducir el fallo. En una partida guiada (`--frames`) el proceso
  termina con error en vez de quedarse esperando. `--debug-jump N:DIR` provoca el fallo en las pruebas.
- Pruebas nuevas: en `roms_and_states`, el fallo provocado devuelve −1, deja el salto en `sf2_dispatch_failure`, el mensaje
  nombra la dirección y el `entry` que falta, la máquina sigue detenida, y cargar un estado anterior la recupera; y
  `game_dispatch_failure` (ctest) arranca el juego sin ventana, provoca el fallo en el cuadro 1.300 y comprueba stderr,
  `errores.log`, que hay un estado de rescate y que con él el juego vuelve a correr (8 pruebas en total).

### Lo que encontraron las partidas completas (en el orden en que apareció)

1. **Ryu, décimo combate, cuadro 35.700: «salto a dirección no decodificada 00e790 desde 00e776».** La heurística del retorno
   por registro (`lea ret(pc),An ; jmp sub`) tomaba por dirección de retorno el puntero a una tabla de animación que el juego
   pasa en `a0` a la rutina 0x4854 (`lea $e77e(pc),a0 ; jmp $4854.w`); decodificó los datos de 0xE77E como código y la última
   «instrucción» se tragó la primera real (0xE790), destino de un `beq.w`. **Corrección en `tools/analyze.py`**: solo es
   retorno por registro si la `lea` y la transferencia están en el mismo tramo lineal y si la subrutina llega a un `jmp (An)`
   con el mismo registro (recorrido del flujo de la subrutina sin entrar en llamadas). Entradas «lea-ret»: de 54 a 28 (las del
   arranque, todas legítimas); 356 «instrucciones» que eran datos desaparecen.
2. **Ryu, combate 12, cuadro 49.707: «salto a dirección no decodificada 0369ee desde 0369ea».** La tabla de saltos de 0x3697E
   tiene 3 entradas pero el análisis la alargó sobre el código de 0x36984 (destino de otras tres tablas) y una «entrada»
   (0x369EC) caía a mitad de `cmp.w` tapando 0x369EE. El fin de tabla solo se cortaba en etiquetas de origen «de confianza» y
   se miraba únicamente el primer motivo de cada etiqueta. **Corrección**: se guardan todos los motivos de cada etiqueta y los
   destinos de otras tablas también acotan una tabla. Arregla de paso 0x2F92C (la segunda entrada de la tabla de 0x2F8EC caía
   en una decodificación falsa en 0x2F92A: habría fallado igual el día que el juego la usara).
3. **Listado de conflictos**: `Analysis.undecoded_successors()` (en el informe de `analyze.py` y en el de cobertura) busca todo
   destino de rama, llamada directa o caída que no sea el comienzo de una instrucción. Salieron 7 tablas con entradas imposibles
   (a mitad de instrucción, códigos de operación inválidos o datos), que se acotaron con `table_end` en `tools/hints.txt`, el
   mecanismo de D12 (cada línea lleva el motivo): 0x1D3FA, 0x1D486, 0x1D50C, 0x2052E, 0x2FEBE, 0x378A4 y 0x8803E.
4. **Blanka y E. Honda tras vencer a M. Bison (cuadros 57.300 y 52.289): «salto a 094c9e desde la instrucción 000ab6».** El
   planificador (0xAB6, `jmp (a0)`) cambia a la tarea del final, creada en 0x94FD8 con `movea.l #$94c9e,a0 ; jmp $0b44.w`: una
   llamada en cola al envoltorio de `trap` que crea tareas. El análisis solo leía los argumentos de tarea en los `jsr`.
   **Corrección**: también en los `jmp` a un envoltorio de `trap`. Es la tarea de los finales de los ocho personajes (con
   Musashi de red de seguridad el arnés la habría interpretado sin avisar; el juego se habría detenido con ese mensaje).
5. **Herramientas de desarrollo ocultas (0x87ABA-0x8A2C8)**: la rutina 0x8A284 imprime un texto que va en línea tras el `bsr` y
   vuelve detrás de él; el análisis decodificaba el texto. Nueva corrección manual `inline_text RUTINA` en `hints.txt` (análisis
   y generador saben que la llamada vuelve tras el texto: fila, columna, atributo, caracteres hasta 0, `/` para otra posición,
   alineado a par); 21 llamadas. Ese código no es alcanzable con entradas (lo activa una bandera de RAM que el juego no pone),
   pero el análisis queda sin conflictos.

Resultado: 43.036 instrucciones (antes 43.216, de las que 356 eran datos y faltaban 176 reales), 1.339 funciones, 11.325
etiquetas, **0 conflictos** y 0 sitios `rt_fatal("... no decodificada")` en el C generado (antes 27). La partida de validación
da el mismo resumen byte a byte (SHA-256 `29339a91…`) y las 8 pruebas de `ctest` pasan.

### Otros arreglos

- `input_mapping` fallaba en este equipo porque había un mando Xbox real conectado: ocupaba el puesto del jugador 1. La prueba
  desactiva ahora los controladores de mandos reales de SDL (HIDAPI, MFi, IOKit, RawInput, WGI) y solo ve sus mandos virtuales.

## Comandos

```bash
make && make test                                   # 8 pruebas
make analyze && grep "destinos sin decodificar" build/coverage.txt       # 0
build/sf2_blind build/rom.bin --frames 60000 --inputs tools/sequences/ryu.txt              # sin red: se detiene si falta algo
build/sf2_blind build/rom.bin --frames 60000 --inputs tools/sequences/ryu.txt --fallback   # con Musashi de red (D19)
```

## Pendiente

- Los fallos de despacho solo se conocen cuando una partida llega al código: la cobertura alcanzada (ver la entrada de
  cobertura) es lo que hay medido; el resto del código sigue dependiendo del análisis estático y de su listado de conflictos.
