# 2026-10-19 · Hito 6 · Cobertura dinámica y validación de las secuencias contra MAME

## Pregunta de partida

Con las 14 secuencias de `tools/sequences/` (ver la entrada de secuencias): ¿qué parte del código estático ejecuta el juego de
verdad, llega alguna vez la tabla de despacho a un destino que el análisis no había previsto, y se comportan las partidas
completas igual que en MAME, cuadro a cuadro, en sonido y en imagen?

## Qué se hizo

### `make coverage` (`tools/coverage.py`)

- Corre cada secuencia en `sf2_blind` **sin la red de seguridad de Musashi** (D46), con sus dipswitches (`--dips` de la
  directiva `#@ dips`), en paralelo (`SF2_COVERAGE_JOBS`, 4 por omisión), y recoge:
  - `--cov`: direcciones de instrucción ejecutadas (las mismas que cuenta el informe estático);
  - `--dispatch-log` (nuevo en el arnés): cada par «instrucción de origen → destino» que pasa por `rt_dispatch`, con el número
    de veces y si el destino estaba en la tabla;
  - `--summary` y `--sound-cmds`: el resumen por cuadro y los comandos de sonido, para la validación contra MAME.
- Analiza la ROM con `tools/analyze.py` y las correcciones de `tools/hints.txt` (lo mismo que alimenta al generador) y clasifica
  cada par de despacho por la instrucción de origen: retorno no estructurado (`rts` a una dirección que no es la siguiente a una
  llamada), excepción (`trap`), `rte` a otra tarea (el trampolín de D19), llamada indirecta (`jsr (An)`, tablas), IRQ2 y salto
  por registro (`jmp (An)`: retornos por registro y el cambio de tarea del planificador en 0xAB6). Un par está «enumerado» si el
  análisis tenía ese destino para ese tipo de origen (puntos de retorno, punteros a código, manejadores de `trap`, etc.).
- Escribe `build/coverage/informe_cobertura.md`: tabla por secuencia, totales, conflictos del análisis (D49), despachos por
  tipo, destinos no enumerados, tramos de código no ejecutado y fallos de despacho. Sale con error si alguna secuencia falla o
  tiene un fallo de despacho.

### `make compare-coverage` (`tools/coverage.py --mame`)

Para cada secuencia, además: MAME 0.289 sin ventana con `tools/mame_blind.lua` y las mismas entradas (resumen por cuadro), los
comandos de sonido del 68000 al Z80 (`SF2_CMDS`, el mismo grifo de escritura que `mame_sound_cmds.lua`) y capturas en los
cuadros de `#@ snap` (cada hito de la partida + 30 cuadros) y uno de cada 2.000; luego el runtime renderiza los mismos cuadros
(`sf2_blind --gfx --png-frames`) y se comparan con `compare_summaries.py`, `compare_sound_cmds.py` y `compare_frames.py`. Los
dipswitches llegan a MAME desde el encendido con una pasada previa de un cuadro que los deja en un `-cfg_directory` propio (D50;
el cfg de `external/mame-data` no se toca). Informe: `build/coverage/informe_mame.md`. Las ejecuciones se pueden repartir
(`--only`) y juntar después (`--report-only --mame-report` lee los resultados del disco).

## Resultados

### Cobertura (código final: reloj exacto de D47, análisis de D49)

| Secuencia | Cuadros | Instrucciones ejecutadas | Distintas | Nuevas (acumulado) | Funciones con entrada ejecutada | Pares de despacho |
|---|---:|---:|---:|---:|---:|---:|
| blanka | 64.965 | 967.503.520 | 20.409 (47,4 %) | 20.409 | 830 | 447 |
| chunli | 67.958 | 1.009.646.048 | 20.778 (48,3 %) | 2.316 | 828 | 442 |
| continuar | 20.998 | 313.865.853 | 14.271 (33,2 %) | 1.735 | 641 | 303 |
| dhalsim | 64.013 | 953.301.306 | 21.137 (49,1 %) | 1.530 | 844 | 458 |
| ehonda | 60.506 | 900.998.828 | 19.752 (45,9 %) | 739 | 803 | 417 |
| guile | 70.818 | 1.051.770.108 | 21.802 (50,7 %) | 737 | 862 | 454 |
| ken | 61.514 | 913.004.449 | 21.227 (49,3 %) | 723 | 859 | 472 |
| retador | 16.001 | 240.385.789 | 13.339 (31,0 %) | 289 | 601 | 308 |
| ryu | 60.916 | 904.008.825 | 21.067 (49,0 %) | 383 | 839 | 457 |
| servicio | 9.470 | 139.884.760 | 2.115 (4,9 %) | 954 | 138 | 132 |
| sin_continuar | 12.125 | 181.311.354 | 13.470 (31,3 %) | 117 | 610 | 290 |
| validacion | 12.001 | 179.364.465 | 13.548 (31,5 %) | 493 | 619 | 297 |
| versus | 16.120 | 241.267.548 | 13.916 (32,3 %) | 110 | 613 | 302 |
| zangief | 65.659 | 980.183.509 | 21.470 (49,9 %) | 1.239 | 855 | 462 |

(«Cuadros» cuenta las IRQ2 entregadas: uno más que la secuencia, el que dispara el final.)

- **31.774 de 43.036 instrucciones (73,8 %)** y **1.087 de 1.339 funciones (81,2 %)**. La partida de validación de los hitos
  2-5 sola llegaba a 13.548 (31,5 %); las otras 13 secuencias añaden 18.226.
- **0 fallos de despacho** en 8.976 millones de instrucciones, **0 instrucciones ejecutadas fuera del análisis** y **0
  conflictos** del análisis.
- Despachos por la tabla: 788 pares origen-destino distintos, 147,8 millones de despachos, 461 etiquetas distintas alcanzadas
  (de 11.325). **Ningún destino despachado sin enumerar** por el análisis para su origen:

  | Tipo | Pares | Veces |
  |---|---:|---:|
  | retorno no estructurado (`rts`) | 75 | 69.032.582 |
  | excepción (`trap`) | 9 | 34.628.885 |
  | `rte` a otra tarea (trampolín) | 2 | 34.478.672 |
  | llamada indirecta | 338 | 8.995.479 |
  | interrupción IRQ2 | 318 | 603.050 |
  | salto por registro (retorno por registro o cambio de tarea) | 45 | 75.391 |
  | arranque (vector de reset) | 1 | 14 |

  En la primera versión del informe los 45 pares de `jmp (An)` salían «no enumerados» (76.672 despachos): eran el cambio de
  tarea del planificador (0xAB6) y los retornos por registro, cuyo destino sí estaba en la tabla como punto de retorno o puntero
  a código, pero el informe no los cotejaba con esas listas; la clasificación «salto por registro» lo hace.
- **Lo que no se ejecuta** (11.262 instrucciones en 672 tramos): el mayor es 0x86E98-0x8A2C8 (2.429 instrucciones, 106
  funciones): las herramientas de desarrollo ocultas, que solo activa una bandera de RAM que el juego no pone. El resto son
  tramos de decenas a unos cientos de instrucciones, la mayoría dentro de funciones que sí se ejecutan: ramas para casos que
  las partidas no provocan (golpes y situaciones concretas de cada personaje, rivales que la CPU no usa con cierta
  dificultad, partes del menú de pruebas que el guion no abre, el único DIVU, el volteo de pantalla). Ese código sigue
  dependiendo del análisis estático y de su listado de conflictos (que está a cero).

### Validación contra MAME

Primera pasada (con el reloj de Musashi de los hitos 1-5): 8 de las 14 secuencias aceptables y **6 con divergencias
sostenidas** (por ejemplo `blanka`: desde el cuadro 32.906, entre un KO y el combate siguiente, 30.961 cuadros distintos hasta
el final). No eran fallos del recompilador sino del reloj: la IRQ cortaba una tarea larga en otro punto, la tarea acababa un
cuadro antes o después y la partida ya no volvía a coincidir. Se corrigió en la máquina (entrada del reloj, D47) y se
regeneraron y validaron las 14.

Con el reloj exacto (dos lotes, 9 + 5 secuencias, juntados con `--report-only --mame-report`):

| Secuencia | Cuadros | Distintos (resumen) | Comandos de sonido | Capturas | MAME (s) |
|---|---:|---:|---|---|---:|
| blanka | 64.964 | 0 | iguales (20.235) | 87 de 87 idénticas | 251 |
| chunli | 67.957 | 0 | iguales (15.839) | 89 de 89 | 254 |
| continuar | 20.997 | 0 | iguales (3.055) | 32 de 32 | 83 |
| dhalsim | 64.012 | 0 | iguales (14.059) | 87 de 87 | 241 |
| ehonda | 60.505 | 0 | iguales (15.323) | 85 de 85 | 237 |
| guile | 70.817 | 0 | iguales (13.627) | 90 de 90 | 275 |
| ken | 61.513 | 0 | iguales (14.291) | 85 de 85 | 233 |
| retador | 16.000 | 0 | iguales (3.749) | 23 de 23 | 64 |
| ryu | 60.915 | 0 | iguales (13.199) | 85 de 85 | 237 |
| servicio | 9.469 | 0 | iguales (49) | 4 de 4 | 40 |
| sin_continuar | 12.124 | 0 | iguales (1.729) | 20 de 20 | 50 |
| validacion | 12.000 | 0 | iguales (1.391) | 17 de 17 | 47 |
| versus | 16.119 | 0 | iguales (3.949) | 24 de 24 | 61 |
| zangief | 65.658 | 0 | iguales (14.727) | 86 de 86 | 244 |

**603.050 cuadros (2 h 48 min de máquina), ninguno distinto**: la RAM de trabajo sin las zonas volátiles, la RAM gráfica, las
entradas y el contador coinciden en todos; los 135.222 comandos de sonido caen en el mismo cuadro con el mismo valor; las 814
capturas son idénticas píxel a píxel. Ya no hace falta la categoría «diferencias aisladas de temporización ya explicadas»: no
queda ninguna. (La partida de validación es además idéntica en el modo estricto, con PC interrumpido, registros y pilas: ver
la entrada del reloj.)

## Comandos

```bash
make coverage                     # unos 2 minutos; build/coverage/informe_cobertura.md
make compare-coverage             # unos 20 minutos con 3 MAME en paralelo; build/coverage/informe_mame.md
python3 tools/coverage.py --mame --roms sf2 --jobs 3 --only ryu ken      # un lote
python3 tools/coverage.py --report-only --mame-report                     # junta los lotes del disco
```

## Pendiente

- La cobertura que falta no es alcanzable con estas secuencias; ampliarla pediría más partidas (otras dificultades, más
  rivales por personaje, el resto del menú de pruebas) con el mismo generador.
