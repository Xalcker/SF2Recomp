# 2026-10-19 · Hito 2 · Mapa de memoria y zona de E/S contrastados con MAME

## Pregunta de partida

El hito 1 dejó la E/S como stubs "razonables" sacados de una lectura rápida de `cps1.cpp`. Para que
el juego se comporte igual que en MAME hay que saber qué devuelve cada dirección que el programa
toca, incluidas las que no están mapeadas, y qué hace cada escritura.

## Qué se hizo

Se reescribió `runtime/mem.c` tras leer `cps1.cpp`, `cps1_v.cpp` e `ioport.cpp` de MAME 0.289 (el
copiado en `external/mame`) y comprobar con Lua lo que MAME devuelve en cada zona
(`space:read_u16` desde un `-autoboot_script`):

| Dirección | MAME | Hito 1 | Ahora |
|---|---|---|---|
| 0xC60180, 0x100000, 0x3FFFFE (sin mapear / ROM sin poblar) | 0x0000 | 0xFFFF | 0x0000 |
| 0x800020 (`nopr`), 0x800010 | 0x0000 | 0 / 0xFFFF | 0x0000 |
| 0x800176 (IN2, puerto de 8 bits) | 0x00FF | 0xFFFF | 0x00FF |
| 0x800140 (CPS-B offset 0) | reg0 × reg0 | último valor escrito | reg0 × reg0 |
| 0x800142-0x80017F salvo 0x172 y 0x176 | 0xFFFF | último valor escrito | 0xFFFF |
| 0x800100-0x80013F (CPS-A, solo escritura) | 0x0000 | 0xFFFF | 0x0000 |
| DSWB (0x80001C) | 0xFCFF | 0xFFFF | 0xFCFF |

- Las zonas sin mapear leen 0 porque el mapa de `cps_state::main_map` no fija `unmap_value` y el
  valor por defecto de MAME es 0. Las 2 lecturas de 0xC60180 del hito 1 devuelven ahora 0x0000.
- `cps1_cps_b_r` devuelve `0xFFFF` para cualquier registro que no sea el ID (0x800172 = 0x0401),
  IN2 (0x800176) o los del multiplicador. Para `sf2` los offsets del multiplicador valen `-1`
  (`__not_applicable__`), y `-1 / 2 == 0` en C, así que el offset 0 (0x800140) devuelve
  `reg0 * reg0`. Es un resto de MAME, no del hardware; se imita porque el objetivo es coincidir con
  MAME y el juego no lee esa dirección (ver tabla).
- Escrituras de byte: el 68000 pone el byte en las dos mitades del bus y la máscara (`mem_mask`)
  indica cuál vale. `cps1_coinctrl_w` solo mira el byte alto (0x800030); los latches de sonido solo
  el bajo (0x800181, 0x800189); los registros CPS-A/B aplican `COMBINE_DATA`. `mem_write8` ahora
  pasa la máscara (`0xFF00` dirección par, `0x00FF` impar).
- Bloqueo de monedas, igual que `ioport_field::frame_update` + `cps1_coinctrl_w`: la salida de
  bloqueo n está activa cuando el bit 10+n de la última escritura en 0x800030 vale 0, y entonces una
  moneda pulsada se lee como no pulsada. El juego escribe 0x00 al arrancar (monedas bloqueadas) y
  0x0C en cada IRQ2 (0x168C, desde la variable 0x2DB(a5)); con 9 créditos vuelve a bloquear
  (`andi.b #$f3` en 0x914). Los valores leíbles se latchean en cada IRQ2 (`mem_latch_inputs`),
  porque MAME actualiza los puertos una vez por cuadro, al empezar el VBlank.
- Dipswitches por defecto de `INPUT_PORTS_START( sf2 )`: DSWA 0xFF, DSWB 0xFC, DSWC 0x9F.
- Cada palabra de la zona de E/S lleva contadores de lecturas y escrituras y el último valor
  (`mem_print_io_table`, opción `--io-table` de los arneses).

## Direcciones de E/S que toca el juego

Tabla producida por `build/sf2_blind --frames 2000 --inputs tools/inputs_sf2.txt --io-table` (2000
cuadros: arranque, aviso, título con una moneda). Ninguna dirección fuera de esta lista se toca en
esa ejecución; los accesos sin mapear son 0.

| Dirección | Lecturas | Escrituras | Último valor | Función |
|---|---:|---:|---|---|
| 0x800000 | 4001 | 0 | 0xFFFB (lectura) | IN1: joysticks y botones 1-3, P1 byte alto, P2 byte bajo |
| 0x800018 | 2000 | 0 | 0xFFFF | IN0 << 8: monedas, servicio, start 1/2, modo test |
| 0x80001A | 2001 | 0 | 0xFFFF | DSWA << 8 |
| 0x80001C | 2001 | 0 | 0xFCFF | DSWB << 8 |
| 0x80001E | 2001 | 0 | 0x9FFF | DSWC << 8 |
| 0x800030 | 0 | 2002 | 0x0C0C | control de monedas (byte alto): contadores y bloqueo |
| 0x800100 | 0 | 2002 | 0x9180 | CPS-A: base de OBJ |
| 0x800102 | 0 | 2 | 0x90C0 | CPS-A: base de SCROLL1 |
| 0x800104 | 0 | 2 | 0x9040 | CPS-A: base de SCROLL2 |
| 0x800106 | 0 | 2 | 0x9080 | CPS-A: base de SCROLL3 |
| 0x800108 | 0 | 2002 | 0x9200 | CPS-A: base de OTHER (row scroll) |
| 0x80010A | 0 | 2001 | 0x9000 | CPS-A: base de la paleta |
| 0x80010C-0x800116 | 0 | 2000-2001 | | CPS-A: scroll X/Y de SCROLL1, 2 y 3 |
| 0x800118-0x80011E | 0 | 2000 | 0x0000 | CPS-A: scroll de estrellas (no usado por SF2, se escribe igual) |
| 0x800120 | 0 | 2000 | 0x0300 | CPS-A: offset del row scroll |
| 0x800122 | 0 | 2002 | 0x003F | CPS-A: control de vídeo |
| 0x800166 | 0 | 2001 | 0x12F8 | CPS-B-11: control de capas |
| 0x800168-0x80016E | 0 | 2000 | 0x0000 | CPS-B-11: máscaras de prioridad 1-4 |
| 0x800170 | 0 | 2001 | 0x003F | CPS-B-11: control de paleta |
| 0x800172 | 2001 | 0 | 0x0401 | CPS-B-11: ID del chip |
| 0x800176 | 2000 | 0 | 0x00FF | CPS-B-11: IN2 (patadas) |
| 0x800180 | 0 | 11 | | latch de sonido |
| 0x800188 | 0 | 10 | | latch 2 de sonido (fade) |

Los registros de vídeo se escriben desde el manejador de IRQ2 (0xAC0-0xAD8, 0xB2A, 0x166A-0x17B4)
copiando "sombras" de la RAM de trabajo (0xFFFF8000 + offset), lo que confirma que en el hito 3 el
vídeo puede leer esas sombras o los registros indistintamente una vez por cuadro.

## Comandos

```bash
make -j8
make check                                   # 600 cuadros, paso cerrado, sigue en OK con el mapa nuevo
build/sf2_blind build/rom.bin --frames 2000 --inputs tools/inputs_sf2.txt --io-table build/io_table.md
tools/mame.sh sf2 -video none -sound none -nothrottle -str 20 -autoboot_script /ruta/sondeo.lua   # read_u16 de cada zona
```

## Resultados

- `make check`: `OK: 600 cuadros sin divergencia (494 entregas de IRQ2), 9382741 instrucciones,
  100608006 ciclos, memoria identica`, 0 accesos sin mapear. Las cifras cambian respecto al hito 1
  porque el cuadro pasa a 167.680 ciclos (ver la entrada del planificador).
- El juego solo toca las direcciones de la tabla; no lee ningún registro CPS-A ni CPS-B salvo el ID
  y IN2, así que el valor exacto de las lecturas "raras" (0x800140) no influye.

## Pendiente

- 0x800188 (fade / latch 2) y 0x800180 son stubs que cuentan: el Z80 llega en el hito 4.
- Los contadores de monedas (`coin_counter`) se registran pero no se muestran en ningún sitio.
