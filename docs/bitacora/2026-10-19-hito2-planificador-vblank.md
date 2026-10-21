# 2026-10-19 · Hito 2 · Planificador de la IRQ2, bucle de espera de VBlank y reloj del código generado

## Pregunta de partida

Dónde y cada cuánto disparar la IRQ2 (VBlank) sobre el código recompilado para que el juego avance
como en la placa, y dónde se queda el juego cuando no tiene nada que hacer.

## Cómo espera el juego al VBlank (del desensamblado, `build/disasm.txt`)

El programa es un núcleo multitarea cooperativo con 16 bloques de tarea de 0x20 bytes en
0xFF0000-0xFF01FF (estado, contador de sueño, SR, PC, USP guardados; pila de usuario inicial en
+0xC, tabla en 0xD68: 0xFF0290, 0xFF0310, …, 0xFF0A10, una cada 0x80 bytes) y variables del
planificador en 0xFF0200-0xFF020F (SP guardado, tarea actual, lista de bloques libres, cuenta, y la
**bandera de VBlank en 0xFF020E**).

| Dirección | Código | Papel |
|---|---|---|
| 0xA30-0xA5C | planificador | `clr.b 0xFF020E`; recorre los 16 bloques con la IRQ enmascarada (`move.w #$2600,sr`) y desenmascarada entre bloques (`move.w #$2000,sr`); si `0xFF020E` se ha puesto a 1 vuelve a empezar; si un bloque tiene estado ≥ 4 lo ejecuta (0xA5E); si no, `bra 0xA30` y sigue dando vueltas |
| 0xAB8-0xB28 | manejador de IRQ2 | guarda registros, copia los registros sombra al CPS-A/B, lee entradas (0x166A), ejecuta los trabajos por cuadro (0x15D4, 0x6264, 0x1990, 0x1AA2, 0x1832), incrementa el contador de cuadros `0x1C(a5)` = 0xFF801C, **pone a 1 la bandera 0xFF020E** (`st -0x7df2(a5)` en 0xAF8) y despierta las tareas dormidas (estado 1) cuyo contador llega a 0 |
| 0xD10-0xD2C | `trap #4` | cesión: guarda SR, PC y USP en el bloque, estado 4 (lista), vuelve al planificador |
| 0xE38-0xEA0 | tarea 0 | bucle principal: `jsr $d10` (cede), consulta 0xFF020E y la cuenta de tareas listas, crea tareas nuevas de la tabla 0xEA2 |

Es decir, **el bucle de espera de VBlank es el par planificador (0xA30-0xA5C) ↔ tarea 0
(0xE38-0xEA0 vía `trap #4`)**, y la bandera que espera es el byte 0xFF020E, que la IRQ2 pone a 1.
Mientras el juego está ocioso la IRQ cae en 0xA30-0xA58 (modo supervisor, SR 0x2000) o en
0xD12/0xE38-0xE9C (modo usuario); en MAME se observa exactamente lo mismo (ver la entrada del
comparador: los pares de PC interrumpido más frecuentes son 0xA54/0xD12, 0xA3C/0xA54…).

Consecuencia para la comparación: entre dos IRQ consecutivas en reposo solo cambian el contador de
cuadros 0xFF801C, un contador BCD en 0xFF82F9 (0x1AD2), un byte en 0xFF8003 y la pila supervisora
(0xFFFFxx), más los bloques de tarea y las pilas de usuario según dónde haya caído la IRQ. Por eso
el resumen por cuadro excluye de la suma principal las zonas 0xFF0000-0xFF0A4F y 0xFFFF00-0xFFFFFF
y las suma aparte (`v=`).

## Planificador en el runtime

- Un cuadro son **167.680 ciclos** de CPU, no 166.666: la pantalla CPS1 va a 8 MHz de píxel con
  512 × 262 ciclos por cuadro (`CPS_PIXEL_CLOCK`, `CPS_HTOTAL`, `CPS_VTOTAL` en `cps1.h` de MAME),
  59,637 Hz, y la CPU a 10 MHz. `CPS1_FRAME_CYCLES` en `runtime/cps1.h`; `--frame-cycles` lo cambia.
- La IRQ2 se marca pendiente al cruzar ese umbral y se **entrega en el siguiente límite de
  instrucción** del código generado (el gancho `rt_hook`), el único punto en que registros y
  memoria están en un estado coherente. El manejador recompilado se ejecuta anidado hasta su `rte`;
  si no vuelve al punto interrumpido (cambio de tarea), se reanuda por `longjmp` al trampolín.
- Un cuadro del resumen = una entrega de IRQ2. Durante el test de RAM del arranque la IRQ está
  enmascarada (~1,8 s, 106 VBlank en MAME) y las peticiones solapadas se funden en una, igual que
  la línea IPL1 de la placa (asertada en VBlank, retirada en el acuse `irqack_r` de MAME).
- MAME asigna 44 ciclos a la excepción; `exception_interrupt()` los suma también.

## Reloj del código generado

El hito 1 estimaba los ciclos con la tabla base por opcode de Musashi (`CYC(n)` por instrucción).
Con eso el arnés a ciegas se desvió de MAME de forma permanente a partir del cuadro 1070 (entrada
al combate): un desfase de un cuadro en una tarea de carga que cruza varios VBlank (byte 0xFF801D
3 frente a 4, bloque de la tarea 9 dormido frente a terminado). Con el reloj exacto de Musashi
(arnés en paso cerrado) no ocurría, así que el generador emite ahora los mismos ajustes
dependientes de los datos que Musashi para el 68000:

| Instrucción | Ajuste (ciclos) |
|---|---|
| Bcc.b no tomado | -2 (la base de la tabla es la del salto tomado) |
| Bcc.w no tomado | +2 |
| DBcc contador no agotado (salta) / agotado | -2 / +2 |
| Scc a registro con condición cierta | +2 |
| ASx/LSx/ROx/ROXx a registro | +2 por posición (cuenta original, también en ROX) |
| MOVEM | +4 por registro (.w), +8 (.l) |
| MULU | +2 por bit a 1 del operando fuente |
| MULS | +2 por transición 0↔1 del operando fuente con signo (bucle de Musashi) |
| TRAP, CHK, división por cero, TRAPV, ILLEGAL, violación de privilegio | la excepción sustituye la base: 34, 40, 38, 34, 34, 34 en total |
| IRQ2 | +44 |

Comprobación: `sf2_lockstep --cycle-check` compara el coste de cada instrucción con el que devuelve
`m68k_execute(1)`. Resultado: **0 instrucciones con coste distinto** en 31,6 millones y el reloj
estimado coincide con el de Musashi al ciclo (ver resultados). La única sutileza fue de
contabilidad del propio arnés: Musashi toma la IRQ dentro del `move #$2000,sr` o `rte` que la
desenmascara (y `m68k_execute(1)` devuelve 60 ó 64 en vez de 16 ó 20), mientras que el
recompilado la entrega en el límite siguiente.

## Comandos

```bash
make -j8
build/sf2_lockstep build/rom.bin --irq-frames 2000 --inputs tools/inputs_sf2.txt --cycle-check --quiet
build/sf2_blind build/rom.bin --frames 4000 --inputs tools/inputs_sf2.txt --summary build/summary_recomp.txt
```

## Resultados

- Antes del modelo de ciclos: 2000 cuadros a ciegas, 353.805.000 ciclos estimados frente a
  353.134.356 de Musashi (+0,19 %); el arnés a ciegas y MAME se separaban para siempre en el
  cuadro 1070.
- Con el modelo: `reloj estimado por el codigo generado: 353134128 ciclos (Musashi 353134128,
  diferencia 0)` y `comprobacion de ciclos: 31595154 instrucciones, 0 con coste distinto`
  (2000 entregas de IRQ2). Frente a MAME, 4000 cuadros a ciegas con solo 12 cuadros distintos,
  todos aislados y seguidos de resincronización (ver la entrada del comparador).

## Pendiente

- El modelo iguala a Musashi, no a la CPU real ni al núcleo de MAME 0.289 (que emula el prefetch
  y la temporización del bus). Los cuadros aislados que difieren de MAME son los que la IRQ
  interrumpe una tarea larga en un punto distinto; desaparecerían con un reloj exacto al de MAME,
  que no es objetivo de este hito.
- Un modo "rápido" que solo compruebe la IRQ en saltos hacia atrás y llamadas (hito 6).
