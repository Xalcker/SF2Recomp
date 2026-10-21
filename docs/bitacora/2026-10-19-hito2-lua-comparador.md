# 2026-10-19 · Hito 2 · Resumen por cuadro desde MAME (Lua) y comparador

## Pregunta de partida

Cómo saber, cuadro a cuadro y sin vídeo, si la máquina recompilada hace lo mismo que MAME. Una traza
instrucción a instrucción no sirve: los relojes no son idénticos y en cada cuadro ocioso la IRQ cae en
otro punto del bucle de espera, así que las trazas difieren siempre aunque el juego sea el mismo.

## Qué se hizo

### Punto de muestra y formato

Un resumen de una línea por cuadro, idéntico en los dos lados (`runtime/cps1.c` y
`tools/mame_blind.lua`), tomado en la instrucción 0xAD0 del manejador de IRQ2 (su primera lectura
de E/S, `move.w $800172.l,0x5e(a5)`):

```
f=1062 pc=0058dc sr=0004 d=00010302,… a=0090b3f8,… in=ff,ffff,ff fc=25 w=af3b9a73 v=aeb35995 g=81150855
```

`pc`/`sr` son los interrumpidos (leídos de la pila, a7+62 y a7+60), `d`/`a` los registros en ese
instante, `in` las entradas que leen los puertos, `fc` el contador de cuadros del juego (0xFF801C),
`w` el hash FNV-1a (palabras de 16 bits) de la RAM de trabajo sin las zonas volátiles, `v` el de las
zonas volátiles (bloques de tarea y pilas de usuario 0xFF0000-0xFF0A4F, pila supervisora
0xFFFF00-0xFFFFFF) y `g` el de la RAM gráfica. Con `--detail`/`SF2_DETAIL=1` se añaden 16 hashes de
4 KiB (`wb=`) y 12 de 16 KiB (`gb=`) para localizar la zona que difiere.

Por qué esas zonas volátiles: entre dos IRQ consecutivas en reposo MAME solo cambia 0xFF801C,
0xFF82F9, 0xFF8003 y la pila supervisora, pero el punto exacto del bucle de espera en que cae la IRQ
decide el estado de los bloques de tarea (tarea 0 en estado 4 ó 8) y el contenido de las pilas de
usuario (sondeo con `read_range` y diff byte a byte, ver la entrada del planificador).

### `tools/mame_blind.lua`

- Tap de lectura sobre 0x800172 (`space:install_read_tap`) filtrado por `cpu.state["CURPC"] ==
  0xAD0`: dispara exactamente una vez por ejecución del manejador (la comprobación del arranque en
  0x4C2 queda fuera). No necesita `-debug`.
- Lee registros con `cpu.state[...]`, la RAM con `space:read_range(…, 8)` (3,7 ms por cuadro para
  los 256 KiB en Lua 5.4) y los puertos con `port:read()`.
- Inyecta las entradas con `ioport.ports[":IN0"]:field(mask):set_value(1)` / `clear_value()`.
- Termina con `manager.machine:exit()` al llegar a `SF2_FRAMES`; `-str` queda como red.
- Capturas opcionales (`SF2_SNAP_FRAMES`, `screen:snapshot`) y volcado de la RAM de un cuadro
  (`SF2_DUMP_FRAME`), con los mismos nombres que los del runtime.

Tropiezos: el tap desaparece si el objeto devuelto se recoge como basura (hay que guardarlo en una
global); los taps no ven las búsquedas de opcode (un tap en 0xAB8 no dispara); `read_range` con
anchura 16 devuelve el orden de bytes del anfitrión; `screen.frame_number` es método;
`emu.register_stop` no existe (`emu.add_machine_stop_notifier`); los errores dentro de un callback
de tap se tragan en silencio (se envuelve en `pcall`).

### `tools/compare_summaries.py`

Compara por número de cuadro. `w`, `g`, `in` y `fc` deben coincidir (DIFERENCIA); `pc`, `sr`,
registros y `v` solo se informan (histograma de pares de PC interrumpido). Imprime las rachas de
cuadros distintos, si vuelven a coincidir, y el detalle de cada una con contexto. Veredicto: código
0 si las rachas son aisladas (≤ `--max-run` = 3 cuadros) y seguidas de resincronización, 1 si hay una
racha más larga o no resincroniza (`--strict` exige igualdad total). `--ram` compara dos volcados
binarios y lista los rangos distintos.

### Flujo

```bash
make run-blind                       # build/summary_recomp.txt (12.000 cuadros, ~1 min)
make compare-mame                    # + build/summary_mame.txt desde MAME (~3,5 min) + comparación
make check-blind                     # paso cerrado con Musashi sobre la misma partida (~3 min)
build/sf2_blind build/rom.bin --frames 1100 --inputs tools/inputs_sf2.txt --dump-frame 1062 --dump-dir /tmp/rec
SF2_DUMP_FRAME=1062 SF2_DUMP_DIR=/tmp/mame SF2_FRAMES=1100 SF2_INPUTS=tools/inputs_sf2.txt \
  tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
    -joystickprovider none -lightgunprovider none -str 40 -autoboot_script tools/mame_blind.lua
python3 tools/compare_summaries.py --ram /tmp/rec/wram_1062.bin /tmp/mame/wram_1062.bin
```

## Resultados (partida completa a ciegas, 12.000 cuadros ≈ 3,4 minutos de máquina)

| Comparación | Cuadros iguales en `w`, `g`, `in`, `fc` | Cuadros distintos |
|---|---:|---|
| `sf2_blind` (reloj estimado) frente a MAME | 11.982 / 12.000 | 18, en 13 rachas aisladas de 1-3 cuadros (787, 1262-1263, 1441, 1561-1563, 1582, 3702, 3796-3798, 6091, 7402, 9575, 9590, 11400, 11635), todas seguidas de resincronización; coinciden del 11636 al 12000 |
| `sf2_lockstep` (reloj de Musashi) frente a MAME | 11.982 / 12.000 | exactamente los mismos 18 cuadros |
| `sf2_blind` frente a `sf2_lockstep` | 12.000 / 12.000 | ninguno, ni siquiera en PC y registros: el reloj estimado reproduce el de Musashi al ciclo |

- En los 18 cuadros distintos la IRQ interrumpió una tarea larga unas instrucciones antes o después
  que en MAME (PC interrumpidos 0x7F27C/0x7F27A, 0x7F646/0x7F634, 0x1304/0x130A…: copias a la RAM
  gráfica y carga del escenario); el estado converge en el cuadro siguiente porque el trabajo
  termina igual. Es la diferencia inherente entre el reloj de Musashi y el núcleo 68000 de MAME
  0.289 (prefetch, bus), no un error del recompilado: `make compare-mame` lo acepta y lo lista.
- Las entradas leídas coinciden en los 12.000 cuadros: la alineación `set_value` en N-1 → lectura en
  N es correcta, y el bloqueo de monedas imitado no altera nada (1 pulso de contador en ambos).
- Antes de completar el modelo de ciclos (ver la entrada del planificador) el arnés a ciegas se
  separaba de MAME para siempre en el cuadro 1070 por un desfase de un cuadro en la carga del
  combate; el comparador lo mostró como racha sin resincronizar y los volcados (`--ram`) señalaron
  el byte 0xFF801D y el bloque de la tarea 9.
- Tiempos: MAME 12.000 cuadros con el script en 202 s (367 % de la velocidad real); `sf2_blind`
  unos 60 s; `sf2_lockstep` unos 180 s.

## Pendiente

- Los 18 cuadros aislados solo desaparecerían con un reloj exacto al de MAME (hito 6 si hace falta).
- Comparar también cuadros de vídeo (hito 3): el script ya guarda capturas PNG por cuadro.

## Verificación independiente (sesión principal, mismo día)

Se ejecutó dos veces `make compare-mame` borrando antes `build/summary_mame.txt`
y `build/summary_recomp.txt` para regenerar los dos resúmenes desde cero.

- Segunda ejecución: reproduce el resultado de arriba exactamente (18 cuadros
  distintos, las mismas 13 rachas aisladas, veredicto aceptable, código 0).
- Primera ejecución: el comparador falló (código 1) desde el cuadro 194. El
  campo de entradas del lado de MAME mostraba un bit pulsado en el cuadro 192,
  cuando la tabla `tools/inputs_sf2.txt` no inyecta nada hasta el cuadro 600.
  El lado recompilado no mostraba esa pulsación. Es decir, la ejecución de MAME
  recibió una entrada que no venía de la tabla.

Conclusión: el resultado del hito es reproducible, pero la ejecución sin
ventana de MAME no es inmune a entradas ajenas a la tabla. Pendiente: hacer
la ejecución de MAME determinista frente a entradas del host (por ejemplo
`-keyboardprovider none -mouseprovider none -joystickprovider none` o grabar
y reproducir las entradas con `-record`/`-playback`) y hacer que el comparador
avise explícitamente cuando el campo de entradas difiera en un cuadro sin
inyección programada.

### Causa y corrección (sesión del hito 2, el mismo día)

- Causa: con `-video none` MAME sigue registrando los dispositivos del anfitrión. `tools/mame.sh sf2
  -video none -sound none -str 2 -verbose` muestra "Keyboard: Using SDL 3.2+, found 1 keyboard",
  "Input: Adding keyboard #1: System keyboard", "Input: Adding mouse #1: System mouse" e "Input:
  Adding lightgun #1: System pointer gun 1". En macOS el proceso recién lanzado toma el foco, así
  que una tecla pulsada en ese momento (la flecha izquierda es la asignación por defecto de P1 Left,
  bit 1 de IN1 → 0xFFFD, justo el valor observado en los cuadros 192-197, unos 3 s de máquina) entra
  en el juego. El lado recompilado solo conoce la tabla, de ahí la diferencia.
- Corrección: `MAME_HEADLESS` en el Makefile añade `-keyboardprovider none -mouseprovider none
  -joystickprovider none -lightgunprovider none`; con ello `-verbose` no registra ningún
  dispositivo. Documentado en `tools/README.md`, en la cabecera de `tools/mame_blind.lua` y en D20.
- Comparador: una diferencia en el campo `in=` es ahora un error específico ("DIFERENCIA DE
  ENTRADAS"), nunca una racha aislada aceptable; con `--inputs tools/inputs_sf2.txt` (que
  `make compare-mame` pasa) indica si en ese cuadro había inyección programada (desalineación entre
  lados) o no (entrada ajena a la tabla). Probado con un resumen de MAME alterado a mano en los
  cuadros 192-197: "cuadro 192: … in=ff,ffff,ff / … in=ff,fffd,ff SIN inyeccion programada", código 1.

### Tres repeticiones desde cero con la corrección

`rm build/summary_mame.txt build/summary_recomp.txt && make compare-mame`, tres veces seguidas, sin
tocar el teclado durante las ejecuciones de MAME (ya sin dispositivos de entrada registrados):

| Repetición | Lado recompilado | Lado MAME | Comparador |
|---|---|---|---|
| 1 | 12.001 cuadros, 179.434.358 instrucciones, 0 episodios de Musashi | 12.000 cuadros en 202 s (384 %) | 18 cuadros distintos, 13 rachas (787, 1262-1263, 1441, 1561-1563, 1582, 3702, 3796-3798, 6091, 7402, 9575, 9590, 11400, 11635), coinciden del 11636 al 12000, código 0 |
| 2 | idéntico | 12.000 cuadros en 202 s (360 %) | idéntico |
| 3 | idéntico | 12.000 cuadros en 202 s (333 %) | idéntico |

Las tres dan exactamente las mismas 13 rachas que la ejecución original y ninguna diferencia de
entradas: el resultado es reproducible y la ejecución de MAME ya no depende de lo que haga el
anfitrión.

