# 2026-10-19 · Hito 1 · Desensamblador por descenso recursivo y tablas de salto

## Pregunta de partida

¿Qué bytes del ROM son código, cuáles son tablas de salto y a qué destinos llevan los ~800
`jmp`/`jsr` indirectos que contó el hito 0? Sin esto no hay funciones que recompilar ni tabla de
despacho.

## Qué se hizo

`tools/analyze.py`: descenso recursivo desde la tabla de vectores (reset 0x40E, IRQ2 0xAB8, los
`rte` en 0x40C y los manejadores de TRAP #0-#15 en 0xB48…0xCCA), siguiendo `bsr/jsr/jmp/bcc/bra/
dbcc`. Cada barrido lineal se detiene en `rts/rte/rtr/jmp/bra/illegal`, al chocar con una tabla,
con un opcode inválido o con código ya decodificado a mitad de instrucción (flujos solapados).

Un evaluador simbólico hacia atrás (dentro del bloque, hasta 16 instrucciones y sin cruzar
llamadas) determina de dónde viene el registro de un salto indirecto: constante, palabra leída de
una tabla, puntero leído de una tabla o índice. Con eso se resuelven estos idiomas:

| Idioma | Resultado |
|---|---|
| `add.w d0,d0 ; move.w t(pc,d0.w),d0 ; jmp/jsr t(pc,d0.w)` | tabla de offsets `.w` relativos a `t` |
| `movea.l t(pc,d0.w),a1 ; jmp/jsr (a1)` | tabla de punteros `.l` (la base puede caer dentro del propio idioma; los índices empiezan en 4) |
| `lea t(pc),a0 ; move.w (a0,d0.w),d0 ; jmp (a0,d0.w)` | idem a través de An |
| `jmp t(pc,d0.w)` con `d0 = n·paso` | secuencia de `bra`/`jmp` de paso fijo |
| `lea ret(pc),a4 ; bra sub … jmp (a4)` | `ret` pasa a ser entrada (retorno por registro, usado en todo el arranque) |
| `pea X(pc)` | X pasa a ser entrada (dirección de retorno empujada a mano; el planificador lo usa) |
| `movea.l #X,a0 ; trap #n` o `jsr envoltorio` (= `trap #n ; rts`) | X es el PC de una **tarea** nueva; también si a0 viene de una tabla de punteros |

Fin de tabla: el primer destino situado tras la tabla, las bases de otras tablas, las etiquetas
alcanzadas por saltos directos, 1024 entradas como máximo y los límites de `tools/hints.txt`
(`table_end BASE FIN`). El análisis se repite hasta tres rondas: las tablas y etiquetas directas
de una ronda acotan las tablas de la siguiente.

Al final se parten las instrucciones en funciones: el cuerpo de una entrada es todo lo alcanzable
por saltos y caída secuencial sin atravesar otra entrada. Los bloques compartidos se duplican en
cada función (ver D11).

### Hallazgo: el juego es un sistema multitarea cooperativo

Las primeras pasadas se quedaban en ~1000 instrucciones porque el flujo pasa por `jmp (a1)` con
`a1` cargado desde RAM. Al leer el código: `trap #0` crea una tarea con PC = a0 (bloque de 0x20
bytes en 0xFF0000+0x20·n: estado, contador, SR, PC, USP, pila inicial), `trap #1` la termina,
`trap #3` cede el control, y el planificador (0xA30-0xAB6) reanuda tareas con `rte` a modo usuario
o con `move.l a4,usp ; move.w #0,sr ; jmp (a1)`. Los PC iniciales de las tareas están en la tabla de
punteros 0xEA2-0xED2, leída con `movea.l $ea2(pc,d2.w),a0 ; jsr $b82` (envoltorio de `trap #7`).
Reconocer los argumentos de los `trap` subió la cobertura de 961 a 43.216 instrucciones.

## Comandos

```bash
make analyze          # build/coverage.txt, build/disasm.txt, build/insns.txt
python3 tools/analyze.py build/rom.bin --report build/coverage.txt --disasm build/disasm.txt
```

## Resultados

Sobre el ROM de 0x100000 bytes (el cuarto segmento, 256 KB, es solo datos):

| Clase | Bytes | % del MB | % de los 768 KB con código |
|---|---|---|---|
| código | 161.702 | 15,42 | 20,56 |
| tabla | 7.316 | 0,70 | 0,93 |
| vectores | 1.024 | 0,10 | 0,13 |
| desconocido | 878.534 | 83,78 | 78,38 |

- 43.216 instrucciones, 1.379 entradas (1.364 funciones con cuerpo), 11.323 etiquetas.
- 797 tablas de offsets `.w` y 23 de punteros `.l`, 3.407 destinos en total.
- 836 `jmp`/`jsr` indirectos: 827 resueltos estáticamente. Los 9 restantes son `jmp (An)` de
  retorno por registro (0x6EE, 0x73A, 0x750, 0x766, 0x7AE, 0x7CA, 0x826, 0xAB6, 0x12818); sus
  destinos ya son entradas y se despachan por tabla en ejecución.
- 59 barridos terminados en opcode inválido y 13 solapamientos: zonas de datos alcanzadas por
  tablas sobreextendidas o punteros especulativos (`pea`). No afectan a la ejecución.
- Cobertura dinámica en el arnés: 1.064 instrucciones distintas en 600 cuadros, 10.118 (23,4 %)
  en 7.200 cuadros (`--cov build/executed_7200.txt`).

## Pendiente

- Seguir punteros a código guardados en RAM (`move.l #dir,d16(An)` en campos de objeto) para
  subir la cobertura estática. Hoy, si el arnés llega a uno no previsto, termina con
  `destino indirecto sin resolver XXXXXX` y basta añadir `entry XXXXXX` a `tools/hints.txt`.
- Cuantificar cuánto del "desconocido" es código real: ejecuciones largas con entradas simuladas
  (hito 2) y comparación con la traza de MAME.
