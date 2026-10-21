# runtime/

El núcleo del juego: estado de la CPU 68000 y primitivas del código generado, mapa de memoria CPS1, la máquina
(reloj de cuadro, entradas, ejecución por cuadros), el vídeo CPS-A / CPS-B-11 (`gfx.c`, `video.c`), la placa de
sonido (`sound.c`, `z80.c`, `ym2151.cpp`, `oki6295.c`), las ROMs en memoria (`romset.c`, `zipread.c`), los
estados guardados (`state.c`) y la fachada `sf2.h` que usan los ejecutables. Es C11 y C++17 (`ym2151.cpp`, sobre
ymfm en `third_party/ymfm`) **sin llamadas al sistema** (decisión D37): no abre archivos, no lee el entorno, no usa
relojes, hilos ni SDL; lo que lee entra como memoria y lo que escribe sale por `FILE *` que le da el anfitrión
(registros de validación) o como búferes. Se comprueba en cada `ctest` (`core_purity`). Lo que supone de la
plataforma está en `sf2_port.h` con un `_Static_assert` por supuesto.

## Hito 6: reloj exacto, despacho y fallos

- **Reloj del 68000 de MAME 0.289** (D47): el código generado (`gen_c.py --clock mame`, por omisión) cuesta lo mismo que el
  núcleo de MAME instrucción a instrucción (`mame_fix` y los ajustes dependientes de los datos, como `cyc_muls_68k` de
  `cpu.h`), el reset dura 34 ciclos (`machine.c`), la entrada a la interrupción se alinea con el reloj E
  (`irq_entry_cycle` en `cpu.h`) y la IRQ se atiende en el punto exacto del microcódigo: cada instrucción deja un código en
  `c->ke` y `rt_hook` calcula hasta qué ciclo tenía que estar afirmada la IRQ para atenderse tras la anterior
  (`Machine.ack`, `acc_end`, `ins_start`, `irq_time`; `recompiler/m68000_ack.txt`). El estado guardado pasa a la versión 2.
- **Despacho**: `dispatch_fn` llama a `rt_dispatch` (`machine.c`), que avisa al gancho `on_dispatch` y, si la dirección no está
  en la tabla, llama a `rt_unknown` y guarda origen y destino (`miss_from`, `miss_to`). Fachada: `sf2_dispatch_failure()`
  (origen y destino del fallo) y `sf2_debug_jump()` (solo pruebas: provoca un salto a una dirección cualquiera).
- **Rendimiento** (sin cambiar un bit de ningún resultado; bitácora `2026-10-19-hito6-rendimiento.md`): `rt_hook` tiene un
  camino rápido (punto de muestreo con selecciones, `poll` solo si llegó el VBlank o hay IRQ pendiente, lo raro fuera de
  línea); `gfx_init` precalcula el mapeador (`Gfx.map`, `gfx_map_fast`) y la paleta a RGB (`Gfx.pal_rgb`,
  `gfx_palette_rgb`); los tilemaps leen cada tile una vez por fila de tiles (`TileInfo`) y se instancian por capa para que
  los tramos de 8/16/32 píxeles se vectoricen; los sprites se recortan antes del bucle. `sf2_port.h` añade
  `SF2_ALWAYS_INLINE` y `SF2_NOINLINE`.

## Hito 5: módulos nuevos y cambiados

| Módulo | Qué es |
|---|---|
| `sf2_port.h` | supuestos de portabilidad comprobados al compilar: bytes de 8 bits, `int` de 32, IEEE-754, complemento a dos, desplazamiento aritmético, orden de bytes (`SF2_LITTLE_ENDIAN`, que fija el orden de `Z80Pair`), compilador con `musttail` (clang ≥ 13, GCC ≥ 15) |
| `crc32.c` | CRC-32 por nibbles, sin tabla global |
| `zipread.c` | lector de zip en memoria (stored y deflate) con inflate propio; valida todos los desplazamientos y el CRC del directorio central |
| `romset.c` | catálogo de las 23 ROMs (CRC, tamaño, destino), carga desde un `RomSource` (zip en memoria, carpeta, navegador) por CRC y no por nombre, informe en español de lo que falta |
| `png.c` | codificador PNG en memoria (el escritor a archivo está en `platform/hostio.c`) |
| `state.c` | `StateStream`: un solo recorrido de campos para guardar y cargar, little-endian |
| `machine.c` | la máquina (antes en `harness/`): planificador de la IRQ2, ganchos, **`machine_step`** (ejecución por cuadros con cesión por `longjmp`, D39), estado guardable; sin instancia global, el `M68K` es el primer miembro de `Machine` |
| `dispatch.c` | `rt_lookup`: tabla de despacho dirección → función generada (va en la biblioteca del código generado) |
| `training.c` | mod de entrenamiento: tiempo, barras, dummy con guion, versus automático, historial de entradas y cajas de golpe (después del hito 6) |
| `sf2.c` / `sf2.h` | fachada: `sf2_create(RomSet)`, `sf2_set_input`, `sf2_run_frame`, `sf2_frame`, `sf2_audio`, `sf2_state_save/load`, `sf2_reset` |

### Fachada `sf2.h`

```c
Sf2 *s;  char err[512];
if (sf2_create(&s, &roms, NULL, err, sizeof err) < 0) ...     // verifica el CRC de la imagen de programa
sf2_set_dips(s, a, b, c);                                      // dipswitches (por omisión 0xFF/0xFC/0x9F, los de MAME)
for (;;) {
    sf2_set_input(s, in0, in1, in2);                           // bits SF2_IN0_*, SF2_IN1_*, SF2_IN2_*; 1 = pulsado
    if (sf2_run_frame(s) < 0) ...                              // hasta el siguiente VBlank; sf2_error(s) si el código falló
    present(sf2_frame(s));                                     // 384 x 224, 0x00RRGGBB
    size_t n; const float *a = sf2_audio(s, &n);               // mono, 48 kHz, la mezcla validada contra MAME
    queue_audio(a, n); sf2_audio_clear(s);
}
```

`Sf2Options.video` / `.sound` a 0 sirven para medir el núcleo solo. `sf2_state_size/save/load` guardan y cargan estados
(ver más abajo); `sf2_state_crc` da el CRC-32 del contenido, que para la misma partida es el mismo en cualquier
plataforma (D45).

### Ejecución por cuadros (`machine.h`)

`machine_run` corre hasta el final (arneses). `machine_step` vuelve tras cada VBlank, en la primera dirección de la tabla
de despacho que no esté dentro de un manejador de IRQ; la pila C se descarta con `longjmp` y se reanuda con
`dispatch_fn(c->pc)`, y el gancho de esa instrucción no se repite (`skip_hook`). Los errores fatales ya no hacen `exit`:
`rt_fatal` deja el mensaje en `Machine.error` y la función que corre la máquina devuelve `MACHINE_ERROR`. Los arneses
añaden por ganchos (`on_insn`, `on_unknown`) la cobertura, el anillo de traza y Musashi como red de seguridad.

### Estados guardados (`state.h`, `sf2.h`)

Cabecera de 24 bytes (marca `SF2R`, versión 1, CRC-32 del código recompilado, longitud, CRC-32 del contenido) y contenido
de unos 460 KiB: CPU (registros, banderas, contadores, PC), planificador, RAM gráfica y de trabajo, registros CPS-A/B, paleta,
latches y bloqueo de monedas, entradas latcheadas, contador de cuadros, tabla de objetos del vídeo, y la placa de sonido
completa (Z80, RAM, YM2151 con `save_restore` de ymfm y su interfaz de tiempo, OKI, cola de latches, cuentas y anillos de
muestras). No se guardan los dipswitches, las entradas ni la secuencia de entradas (son del anfitrión). Se rechazan marca,
versión, código, longitud, CRC y valores fuera de rango; la carga es atómica (D40). Probado: otra máquina recién creada que
carga un estado sigue idéntica 360 cuadros (vídeo y audio), rebobinar repite lo mismo, y estados dañados no cambian nada.

## `cpu.h`

- `struct M68K`: `d[8]`, `a[8]`, `pc`, `sp_other` (el puntero de pila inactivo: USP cuando S=1,
  SSP cuando S=0), banderas `x n z v c` como bytes 0/1, `s`, `t`, `im` (máscara de IRQ),
  `stopped`, `irq_pending`, contadores `icount`/`cycles` y el puntero a su `Mem`.
- Macros para el código generado: `INSN(dir)` fija `c->pc` y, con `-DRECOMP_HOOK`, llama a
  `rt_hook(c)` (el arnés); `CYC(n)` acumula ciclos con `-DRECOMP_CYCLES`. El generador emite la base
  de la tabla de Musashi más los ajustes dependientes de los datos (Bcc, DBcc, Scc, desplazamientos,
  MOVEM, MULU/MULS, excepciones), de modo que el reloj coincide al ciclo con Musashi (ver la bitácora
  del planificador del hito 2); `cyc_mulu()`/`cyc_muls()` calculan los de las multiplicaciones.
- Memoria: `rd8/16/32`, `wr8/16/32`, `push/pop`.
- SR: `get_sr/set_sr/set_ccr`; `set_s()` intercambia A7 y `sp_other` al cambiar de modo.
- Excepciones: `exception(c, vector, pc_a_apilar)` y `exception_interrupt(c, nivel)` (suma los 44
  ciclos de la excepción) apilan el marco de 3 palabras del 68000 y dejan en `c->pc` el manejador;
  `dispatch_fn(c)` devuelve la función que contiene `c->pc` mediante `rt_lookup()` y, si no existe,
  llama a `rt_unknown(c)` para que el anfitrión ejecute el código ajeno (Musashi en `harness/blind.c`)
  hasta volver a una dirección conocida; `dispatch_call(c)` la llama.
- Operaciones con banderas, generadas por tamaño con `DEF_SIZE_OPS`: `op_add/sub/cmp/addx/subx/
  neg/negx/logic`, más `op_mulu/muls/divu/divs`, `op_abcd/sbcd/nbcd`, `op_asl/asr/lsl/lsr/rol/
  ror/roxl/roxr`, `op_chk` y `cond()`.  Reproducen exactamente la semántica de Musashi, incluidos
  los bits "indefinidos" del manual (decisión D8).

Funciones que debe aportar el programa anfitrión: `rt_hook`, `rt_fatal`, `rt_stopped`, `rt_lookup` y
`rt_unknown`; para el juego y `sf2_blind` las define `machine.c` (y `rt_lookup` `dispatch.c`), el paso cerrado
`sf2_lockstep` trae las suyas.

## `mem.h` / `mem.c`

| Rango | Contenido |
|---|---|
| 0x000000-0x0FFFFF | ROM de programa (1 MiB, cuatro pares de chips) |
| 0x100000-0x3FFFFF | ROM sin poblar: lee 0x0000 |
| 0x800000-0x8001FF | E/S (tabla abajo) |
| 0x900000-0x92FFFF | RAM gráfica, 192 KiB |
| 0xFF0000-0xFFFFFF | RAM de trabajo, 64 KiB |
| resto | sin mapear: lee 0x0000, la escritura se descarta (valor por defecto de MAME, comprobado) |

- Los accesos de 32 bits se parten en dos de 16, como el bus real.  Las escrituras de byte ponen el
  byte en las dos mitades del bus y pasan una máscara (`0xFF00` par, `0x00FF` impar) a la E/S, como
  `mem_mask` en MAME.  Un acceso de palabra a dirección impar se compone con dos bytes (no se emula el
  error de dirección; Musashi está configurado igual).
- Cada `Mem` tiene su RAM propia: el arnés en paso cerrado usa dos instancias (recompilado y Musashi) y
  compara el log de escrituras de 16 bits de cada instrucción (`wlog`) y la RAM completa cada segundo.
- `mem_compare()` devuelve la primera dirección distinta entre dos instancias.
- Cada palabra de la zona de E/S acumula lecturas, escrituras y último valor; `mem_print_io_table()`
  escribe la tabla en markdown (opción `--io-table` de los arneses).

### Zona de E/S (placa CPS-B-11, `{"sf2", CPS_B_11, mapper_STF29, 0x36}` en MAME)

| Dirección | Comportamiento |
|---|---|
| 0x800000-0x800007 lectura | `IN1`: joysticks y botones 1-3, P1 bits 0-7, P2 bits 8-15, activo a nivel bajo |
| 0x800018 lectura | `IN0 << 8 \| 0xFF`: monedas (bits 0-1), servicio (2), start 1/2 (4-5), modo test (6) |
| 0x80001A/1C/1E lectura | `DSWA/DSWB/DSWC << 8 \| 0xFF`; por defecto 0xFF, 0xFC, 0x9F (`INPUT_PORTS_START( sf2 )`) |
| 0x800020 lectura | 0 |
| 0x800030-0x800037 escritura | control de monedas (solo byte alto): bits 8-9 contadores (se cuentan los flancos), bits 10-11 bloqueo de la moneda 1/2 (0 = bloqueada: la moneda pulsada se lee como no pulsada) |
| 0x800100-0x80013F | registros CPS-A, solo escritura (`COMBINE_DATA`); la lectura devuelve 0 |
| 0x800140-0x80017F | registros CPS-B: lectura de 0x800172 = 0x0401 (ID del CPS-B-11), 0x800176 = `IN2` (botones 4-6, byte bajo, 0x00FF sin pulsar), 0x800140 = reg0 × reg0 (resto del multiplicador en MAME), el resto lee 0xFFFF |
| 0x800180-0x800187 escritura | latch de sonido (byte bajo; si solo se escribe el alto, el alto), se cuenta y se pasa a `sound_latch_cb` |
| 0x800188-0x80018F escritura | latch 2 de sonido / fade (solo byte bajo), se almacena y se pasa a `sound_latch_cb` |

`sound_latch_cb(ctx, latch, valor)` lo instala `machine.c` (`machine_attach_sound`) para entregar
cada escritura a la placa de sonido con el ciclo del 68000.

Las entradas se guardan como "pulsadas" (`in0/in1/in2`) y se latchean en cada IRQ2
(`mem_latch_inputs`, aplicando el bloqueo de monedas), porque MAME actualiza los puertos una vez por
cuadro al empezar el VBlank.

RAM de paleta (hito 3): `m->palette[0xC00]` es la RAM propia del CPS-B. Cada escritura de la base de
paleta (0x80010A) copia desde la RAM gráfica (base = registro × 256 alineada a 0x400) las páginas de 512
entradas habilitadas en el control de paleta (0x800170, bits 0-5: OBJ, SCROLL1-3, estrellas 1-2); una
página no habilitada se salta en la RAM gráfica solo si ya se copió alguna antes (`mem_palette_copy`,
calcado de `cps1_build_palette`). Los valores son crudos (`bbbbRRRRGGGGBBBB`); la conversión a RGB la hace
`video.c`. `mem_compare` incluye esta RAM. Los índices de los registros CPS-A y CPS-B-11 están en `mem.h`
(`CPSA_*`, `CPSB11_*`).

## `cps1.h` / `cps1.c` (hito 2)

Máquina mínima: constantes del reloj (`CPS1_FRAME_CYCLES` = 167.680 ciclos, 59,637 Hz), punto de
muestra (`CPS1_SAMPLE_PC` = 0xAD0), secuencia de entradas por cuadro y resumen por cuadro.

- `cps1_parse_inputs(texto, nombre, err, n)`: interpreta `desde hasta tecla` (ver `tools/inputs_sf2.txt`; teclas `coin1`,
  `start1`, `p1_right`, `p1_lp`, `p1_lk`, …) desde un texto; el archivo lo lee el anfitrión
  (`cps1_load_inputs` de `platform/hostio.c`).  `cps1_apply_inputs(frame)` fija `in0/in1/in2`.
- `cps1_on_irq2()`: llamar al entregar la IRQ2; cuenta el cuadro y aplica sus entradas.
- `cps1_sample()`: llamar cuando el PC llega a 0xAD0 (primera lectura de E/S del manejador de
  IRQ2); escribe la línea `f= pc= sr= d= a= in= fc= w= v= g=` (`--detail` añade `wb=`/`gb=` por
  bloque).  `w` es FNV-1a de la RAM de trabajo sin
  las zonas volátiles (bloques de tarea y pilas de usuario 0xFF0000-0xFF0A4F, pila supervisora
  0xFFFF00-0xFFFFFF, que van en `v`); `g` es el de la RAM gráfica; `fc` el contador de cuadros del
  juego (0xFF801C).  `tools/mame_blind.lua` produce exactamente la misma línea desde MAME.
- `manual_in0/1/2`: bits pulsados por el anfitrión (visor con `--keyboard`), que `cps1_apply_inputs`
  suma a los de la tabla.

## `gfx.h` / `gfx.c` (hito 3)

ROMs gráficas y mapeador de bancos. `gfx_init(g, romset)` toma las doce ROMs de 512 KiB del `RomSet` (por CRC:
`sf2-5m.4a`... o `sf2_06.bin`..., posiciones en `romset.c`) y reconstruye en memoria la región "gfx" de 6 MiB de MAME (`ROM_LOAD64_WORD`: una palabra de cada ROM cada 8 bytes); la
decodifica entera a pens (un byte 0-15 por píxel, 12 MiB) en unidades de 16 píxeles (8 bytes: planos
del bit 0 al 3 en los bytes 0-3 para los ocho píxeles de la izquierda y 4-7 para los de la derecha, bit 7
= píxel izquierdo). Un tile de 8x8 son 64 bytes (media unidad por fila: las columnas pares del tilemap
usan la mitad izquierda y las impares la derecha), uno de 16x16 128 bytes (una unidad por fila) y uno de
32x32 512 bytes (dos unidades por fila). `gfx_row8/16/32(tile, fila)` devuelven los pens de una fila.

`gfx_map(tipo, código)` es `gfxrom_bank_mapper` con `mapper_STF29_table`: identidad para los sprites
(códigos 0x0000-0x8FFF) y, para las capas, SCROLL3 0x0400-0x07FF → tile 0x2000 + código, SCROLL1
0x4000-0x4FFF → 0x10000 + código, SCROLL2 0x2800-0x3FFF → 0x8000 + código (todas en el tercer grupo de
ROMs); fuera de rango devuelve -1 (tile transparente). Detalle en la bitácora
`2026-10-19-hito3-roms-graficas-mapeador.md`. Desde el hito 6 `gfx_init` lo tabula para los 65.536 códigos de cada tipo
(`gfx_map_fast`) y tabula también la conversión de las palabras de paleta a RGB (`gfx_palette_rgb`).

## `video.h` / `video.c` (hito 3)

Renderizador calcado de `cps1_v.cpp` (decisión D21). `Video` guarda la tabla de objetos latcheada
(`obj`), la paleta convertida (`rgb`), el bitmap de pens (`pix`), el de prioridad (`pri`) y el cuadro final
(`frame`, 384 x 224 píxeles 0x00RRGGBB) más información del último cuadro (`order`, `enabled`, `sprites`,
`rowscroll`, `marked`, `blocked`).

- `video_vblank(v, mem)`: llamar al empezar cada VBlank (gancho `on_vblank` de `machine.c`):
  `video_render` y después `video_latch_obj` (copia los 0x800 bytes de la base OBJ), el orden de
  `vblank_begin` de MAME. El cuadro N que escribe `on_sample` es el de este VBlank.
- `video_render(v, mem)`: paleta a RGB (brillo `0x0F + 2·b` sobre `0x2D`, división entera; tabulada en `gfx_init`), fondo con el
  pen 0xBFF, y las cuatro capas en el orden de los bits 6-13 del control de capas (0x800166; 0 = sprites):
  tilemaps de 64 x 64 tiles (SCROLL1 8x8 = 512 px, SCROLL2 16x16 = 1024 px con row scroll opcional desde
  la zona OTHER, SCROLL3 32x32 = 2048 px), habilitación por los bits 0x08/0x10/0x20 del control de capas
  y 2-3 del control de vídeo, coordenadas crudas de la placa (la zona visible empieza en (64, 16)).
  Sprites desde `obj` hasta la marca de fin `0xFFxx`, bloques de hasta 16 x 16 tiles, pen 15
  transparente. Prioridad: la capa dibujada justo antes de los sprites marca el bit 0 de `pri` en los
  pens que su máscara de grupo (0x800168-0x80016E, grupo = bits 7-8 del atributo) declara por delante;
  un píxel de sprite se escribe si `pri` no es 1 ni 31 y deja `pri` = 31, así que la primera entrada de
  la tabla queda encima de las siguientes (bit 31 implícito de `prio_transpen`).
- `video_encode_png(v, &buf, &len)` / `png_encode_rgb` (`png.c`): PNG RGB de 8 bits sin dependencias (zlib "stored")
  en memoria; `platform/hostio.c` lo escribe a archivo.
- Pantalla ancha (D53, D56, D57): `video_set_width(v, w)` / `sf2_set_video_width` fijan el ancho del cuadro entregado (384 a `SF2_SCREEN_W_MAX` = 800, par; 384
  = apagada y todo idéntico a MAME). `video_scene_wide` decide por la RAM del juego (0xFF8001/05/09) si la escena
  se ensancha (combate, bonus y demostración: presentación de ronda, pelea, victoria y recuento); las demás se dibujan en 384 y se
  centran con barras del color del fondo, así que el ancho del cuadro no cambia entre escenas. `Video.w` es el ancho entregado y
  `rw`/`rx0` la geometría del último cuadro; los sprites que cruzan la X 512 reaparecen por la izquierda. El ancho no es parte del
  estado guardado. Cómo está hecha y sus límites en `docs/WIDESCREEN.md`.
- Lógica ancha (D58): `sf2_set_wide_logic` pone `cpu.mod_wide` (píxeles extra por lado) y los parches de `recompiler/mod_patches.txt`
  (aplicados por `gen_c.py`) lo usan en las paredes de los objetos y en la cámara; 0 = juego original, idéntico por CRC de estado.
- HUD en los bordes (D55): `video_set_hud_edges` / `sf2_set_hud_edges`; con la vista ancha mueve ±64 px las ranuras 0-63 y los
  marcadores de la tabla de objetos (`hud_side` en `video.c`), nunca los luchadores.
- Relleno de extremos (D54): con la vista ancha, `art_run_find` / `fill_row_cols` buscan en la RAM gráfica el hueco del anillo de
  64 columnas de SCROLL2/3 y lo rellenan, fila a fila, con las columnas de arte reflejadas, solo fuera de la ventana de la
  recreativa (crudas 64-447); `Gfx.blank` marca los tiles transparentes.
- HFR (`docs/HFR.md`): `Video.prev` / `Video.cur` (`VideoSnap`) guardan dos instantáneas por VBlank y `sf2_render_interp(s, t256)` dibuja entre ambas; ni la partida ni el audio cambian.
- Stage completo (D59): `sf2_set_stage_full` pone `cpu.mod_fixcam` y `mod_wide`; `Video.xsigned` lee la X de los sprites con signo (`docs/ULTRAWIDE.md`).
- Mod de entrenamiento (`training.c`, `training_priv.h`, `training.h`, `docs/ENTRENAMIENTO.md`): `on_vblank` de `sf2.c` llama a `training_vblank` justo antes de entregar la IRQ2; retoca el temporizador (`0xFF8ACE/F`), la vida (`jugador+0x30`) y las entradas manuales (`cps.manual_in*`), y calcula el historial de entradas y las cajas (`training_boxes`, `docs/HITBOXES.md`). Fachada: `sf2_training_set/get/history/pad/boxes`. Apagado (`enabled = 0`, `track_inputs = 0`) no toca nada. Fuera del estado guardado.
- No implementado: capa de estrellas y volteo de pantalla (SF2 no los usa, D24; aviso si se activara).
- Depuración: `Video.layer_mask` dibuja solo las capas pedidas (bit 0 sprites, bits 1-3 SCROLL1-3); `sf2_blind` lo
  rellena desde `SF2_VIDEO_LAYERS` (el núcleo ya no lee el entorno).

## Sonido (hito 4)

Detalle y validación en las entradas `docs/bitacora/2026-10-19-hito4-*`; decisiones D27-D35. Mapa del Z80 (el
`sub_map` de `cps1.cpp`): 0x0000-0x7FFF ROM, 0x8000-0xBFFF banco de ROM (bit 0 de 0xF004), 0xD000-0xD7FF RAM,
0xF000/0xF001 YM2151, 0xF002 OKI, 0xF006 pin 7 del OKI, 0xF008/0xF00A latches del 68000 (comando y fade); la
INT es la línea IRQ del YM2151 (temporizador A, 249,7 Hz). El núcleo Z80 es propio (D27), no uno de terceros.

### `z80.h` / `z80.c`

Intérprete Z80 completo (página principal, CB, ED, DD/FD y DD CB / FD CB, banderas no documentadas, MEMPTR,
registro Q). `z80_step` ejecuta una instrucción, una aceptación de INT o un ciclo de HALT y suma sus
ciclos a `z->cycles`; cada acceso a `Z80Bus` (`fetch`, `read`, `write`, `in`, `out`) recibe el ciclo
absoluto del comienzo de su ciclo M. INT por nivel (`z80_set_irq`), mirada en cada límite con IFF1 y no
justo tras EI; IM 1 (y IM 0/2 con 0xFF en el bus) en 13 ciclos. `z80_disasm` desensambla una
instrucción (lo usa `tools/z80dis.c` y la traza `SF2_Z80_TRACE`).

### `ym2151.h` / `ym2151.cpp`

YM2151 de ymfm con interfaz C. El tiempo son ciclos del chip (= del Z80): `ym2151_write/read(y, now,
sync, offset, dato)` con el ciclo del acceso; ocupado 64 ciclos tras cada dato; los temporizadores y las
escrituras aplazadas (registro de modo, recálculo de IRQ) son sucesos que se consultan con
`ym2151_next_event` y se ejecutan con `ym2151_run_event`; `ym2151_irq` es el nivel de la salida IRQ y
`ym2151_generate` produce muestras estéreo enteras (una cada 64 ciclos, 55.930 Hz).

### `oki6295.h` / `oki6295.c`

OKI MSM6295 como `okim6295.cpp` de MAME: comandos, cuatro voces ADPCM, lectura de estado, y
`oki6295_generate` = una muestra (escala de MAME, voces sumadas en float). Pin 7 alto (7.575 Hz).

### `sound.h` / `sound.c`

La placa: `sound_init(s, romset)` toma `sf2_9.12a` (región de MAME: 0x0000-0x7FFF y dos bancos de 16 KiB
en 0x10000), `sf2_18.11c` y `sf2_19.12c` del `RomSet`; `sound_reset` la reinicia. `sound_latch_write(s, latch, valor, ciclo68)` encola una escritura
del 68000; `sound_run_until(s, ciclo68)` hace avanzar el Z80 hasta el ciclo del Z80 equivalente
(`sound_z80_cycle`), procesando en orden las escrituras de latch, los sucesos del YM2151 y la actualización
periódica de 50 Hz. Antes de cada acceso a un chip se generan sus muestras hasta el instante del acceso; en
cada actualización periódica se mezcla el bloque nuevo de 48 kHz con el remuestreador "lofi" de MAME
(ganancias 0,35, 0,35 y 0,30) y se entrega a `on_audio`. Salidas opcionales: `f_events` (sucesos como
`tools/mame_audio.lua`), `f_ym`, `f_oki`, `f_mono` (float32) y `f_chunks`. `sound_report` imprime contadores.

Depuración: `sound_set_trace(s, f, desde, hasta)` (ciclos del Z80) traza cada paso del Z80 en `f`; `sf2_blind` lo activa
con `SF2_Z80_TRACE=desde:hasta`.

La tabla de interpolación se calcula sin contracción a FMA y el remuestreo con ella, como el binario de
MAME en arm64: así la mezcla coincide bit a bit (D31). Por eso `sound.c` se compila con `-O2` y sin opciones
de float.

`oki6295.c` ya no usa `pow()`: la tabla de pasos de la señal (`oki6295_step_table`) es la de MAME escrita a mano y una
prueba la compara con `floor(16 · 1,1^n)`.

### WAV

`platform/wav.c` (fuera del núcleo: escribe archivos): escritor WAV PCM de 16 bits; `wav_add_float` convierte como la
grabación de MAME (`int(x · 32768)` saturado).
