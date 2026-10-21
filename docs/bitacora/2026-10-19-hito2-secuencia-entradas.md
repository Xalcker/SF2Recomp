# 2026-10-19 · Hito 2 · Secuencia de entradas y partida completa a ciegas; cierre del hito

## Pregunta de partida

Hacer que el juego atraviese el arranque, el modo atract y una partida completa sin vídeo, con las
mismas entradas en el runtime y en MAME, y comprobar que el resumen por cuadro coincide.

## Formato y alineación

`tools/inputs_sf2.txt`: `desde hasta tecla`, cuadros inclusive, donde el cuadro N es la N-ésima
entrega de la IRQ2 (el cuadro 1 llega al terminar el test de RAM, ~1,8 s de máquina, VBlank 106 de
MAME). Teclas `coin1 coin2 service start1 start2 test`, `p1_up/down/left/right`, `p1_lp/mp/hp`
(puñetazos, IN1), `p1_lk/mk/hk` (patadas, IN2) y las `p2_*`; `runtime/cps1.c` y
`tools/mame_blind.lua` tienen la misma tabla de nombres → (puerto, bit).

- Runtime: `cps1_on_irq2()` aplica las entradas del cuadro N al entregar su IRQ2 y las latchea con
  el bloqueo de monedas vigente; el manejador las lee en 0x166A (0x800018, 0x800000/1, 0x800177).
- MAME: los puertos se actualizan en `vblank_begin` → `frame_update` → `ioport_manager::
  frame_update_callback`, antes de la IRQ2; un `set_value` en el cuadro N se lee en N+1 (sondeo:
  `set_value` en la muestra 300 → `in0=fe` en la 301). Por eso el script aplica las entradas del
  cuadro N+1 durante la muestra del cuadro N.
- Bloqueo de monedas: el juego escribe 0x00 en 0x800030 al arrancar (bloqueo) y 0x0C en cada IRQ2
  (desbloqueo); MAME ignora una moneda bloqueada (`coin_lockout` activado por defecto) y el runtime
  hace lo mismo.

## Cómo se diseñó la secuencia

Con capturas de MAME en cuadros elegidos (`SF2_SNAP_FRAMES`, `screen:snapshot`) y mirando los PNG:

| Cuadro | Pantalla en MAME |
|---|---|
| 100 | test de RAM (SCR1/2/3 RAM OK, OBJECT) |
| 600 | se inserta la moneda (`in0=fe`, crédito registrado en 0xFF82DD) |
| 650 | pantalla de aviso "WARNING … for use in all countries excluding USA…" con CREDIT=1 |
| 760 | fundido a negro |
| 900-1000 | título "PUSH 1P START", CREDIT=1 |
| 1000 | start (un start en el 720, durante el aviso, no se registraba) |
| 1100 | PLAYER SELECT con el cursor en Ryu |
| 1200 | puñetazo: Ryu confirmado |
| 1600 | ROUND 1, Ryu contra Blanka (escenario de Brasil) |
| 1600-5400 | combate: patrón de avance y golpes (`p1_right` 120 cuadros con `p1_lp/mk/hp/lk/mp` cada 20; a veces `p1_left`+`p1_hk`, `p1_down`+`p1_hp`), Ryu pierde los dos asaltos (2.700 puntos) |
| 5600 | KO del segundo asalto |
| 6200 | CONTINUE? con la cuenta atrás (sin monedas) |
| 7000 | fin de partida: vuelve el atract (introducción Ryu/Ken) |
| 8000 | título sin crédito; 9000 negro; 10000 combate de demostración Blanka contra Guile; 12000 fundido del título |

La secuencia tiene 154 eventos y termina en el cuadro 5500; a partir de ahí el juego sigue solo. La
partida completa cabe en 12.000 cuadros (3,4 minutos), que es el valor por defecto de `BFRAMES`.

## Comandos

```bash
make -j8 && make check
make run-blind                                    # build/summary_recomp.txt, build/io_table.md
make compare-mame                                 # añade build/summary_mame.txt y compara
make check-blind                                  # sf2_lockstep --irq-frames 12000 con las mismas entradas
SF2_SNAP_FRAMES=1600,5600,6200,7000 SF2_FRAMES=7100 SF2_INPUTS=tools/inputs_sf2.txt SF2_SUMMARY=/tmp/s.txt \
  tools/mame.sh sf2 -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none \
    -joystickprovider none -lightgunprovider none -str 150 -autoboot_script tools/mame_blind.lua
```

## Resultados

```
build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt --summary build/summary_recomp.txt --io-table build/io_table.md
OK: 12001 cuadros, 179434358 instrucciones (0 en Musashi, 0 episodios), 2029934136 ciclos estimados, 12001 IRQ atendidas, 12000 lineas de resumen
accesos E/S: 96008 lecturas, 265410 escrituras, 5 sin mapear
cobertura dinamica: 13548 instrucciones distintas ejecutadas de 43216 estaticas (31.3%)

build/sf2_lockstep build/rom.bin --irq-frames 12000 --inputs tools/inputs_sf2.txt --cycle-check --quiet
OK: 12106 cuadros sin divergencia (12001 entregas de IRQ2), 179434358 instrucciones, 2029934136 ciclos, memoria identica
reloj estimado por el codigo generado: 2029934136 ciclos (Musashi 2029934136, diferencia 0)
comprobacion de ciclos: 179422357 instrucciones, 0 con coste distinto

python3 tools/compare_summaries.py build/summary_recomp.txt build/summary_mame.txt
DIFERENCIA: 18 cuadros con RAM/entradas distintas; primero en el cuadro 787 (g)
rachas de cuadros distintos: 787, 1262-1263, 1441, 1561-1563, 1582, 3702, 3796-3798, 6091, 7402, 9575, 9590, 11400, 11635
vuelven a coincidir desde el cuadro 11636 hasta el final (12000)
VEREDICTO: aceptable, 13 rachas aisladas de como mucho 3 cuadros, todas seguidas de resincronizacion
```

- Ninguna dirección fuera de la tabla de E/S; los 5 accesos sin mapear son lecturas de 0xC60180
  (ya vistas en el hito 1), que devuelven 0x0000 como en MAME. 696 comandos de sonido al latch.
- Musashi no intervino ni una vez como red de seguridad en toda la partida: el análisis estático
  cubre todo el código recorrido (31,3 % de las instrucciones, frente al 23,4 % del atract solo).
  `tools/hints.txt` sigue vacío.
- La cobertura dinámica sube de 10.118 a 13.548 instrucciones distintas con la partida.

## Estado del hito 2

Cumplido el criterio del plan: el juego atraviesa el arranque, el atract y una partida completa a
ciegas, y el resumen por cuadro coincide con MAME en 11.982 de 12.000 cuadros; los 18 restantes
están explicados (la IRQ corta una tarea larga en otra instrucción y el estado converge al cuadro
siguiente) y el comparador los acepta como aislados. Entregables: `runtime/mem.c` (mapa de E/S
completo), `runtime/cps1.c` (máquina mínima), `harness/blind.c` (`sf2_blind`, Musashi de reserva),
`harness/main.c` ampliado (`--inputs`, `--summary`, `--irq-frames`, `--cycle-check`),
`recompiler/gen_c.py` (modelo de ciclos), `tools/mame_blind.lua`, `tools/compare_summaries.py`,
`tools/inputs_sf2.txt`, objetivos `run-blind`, `compare-mame` y `check-blind` del Makefile,
decisiones D15-D19, READMEs y estas cuatro entradas.

## Pendiente

- Secuencia alternativa que gane asaltos (o `p2_*` para un segundo jugador) para cubrir más código:
  bonus stages, continuar con moneda, otros personajes.
- Hito 3: vídeo. Los registros sombra de la RAM de trabajo que copia la IRQ2 (0xFFFF8000 + 0x2A,
  0x32, 0x34, 0x52-0x5E, 0x60-0x70) y la tabla de E/S de `build/io_table.md` son el punto de partida.
