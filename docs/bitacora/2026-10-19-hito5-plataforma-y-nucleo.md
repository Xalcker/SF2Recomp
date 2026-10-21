# 2026-10-19 · Hito 5 · Capa de plataforma separada del núcleo y auditoría de portabilidad

## Pregunta de partida

Qué en `runtime/` y `harness/` ata el juego a este Mac y a este arnés (rutas, archivos, entorno, orden de bytes, tipos,
variables globales, cómo se vuelve al bucle de la ventana), y cómo dejar el núcleo en C/C++17 sin llamadas al sistema con
la entrada, la ventana, el audio y los archivos en un módulo aparte, sin perder ni un bit de lo validado.

## Qué se hizo

### La frontera

- **Núcleo** (`runtime/`, decisión D37): CPU recompilada, máquina, vídeo, sonido, ROMs en memoria, estados. Solo biblioteca
  estándar y los `FILE *` que da el anfitrión para registros de validación. Se comprueba con `ctest` (`core_purity`): `nm -u`
  sobre `libsf2_base.a`, `libsf2_core.a` y `libymfm.a` y rechazo de `fopen`, `getenv`, `exit`, `abort`, `time`, `clock_gettime`,
  `pthread_*`, `SDL_*`, `stdout/stderr`, etc. Probada en negativo con `libsf2_hostio.a`, que sí los usa.
- **Plataforma sin SDL** (`platform/`): `hostio` (archivos con stdio, carga de ROMs desde zip o carpeta, PNG), `wav`, `config`.
- **Aplicación SDL3** (`app/`): ventana, teclado, mandos, audio, ritmo, estados guardados en disco, página web.
- **Arneses** (`harness/`): lo que ya eran, ahora sobre la misma máquina del núcleo.

### Lo que no cumplía y cómo se arregló

| Dependencia no portable encontrada | Dónde | Solución |
|---|---|---|
| lectura de ROMs por carpeta con `fopen` y nombres fijos (`sf2/sf2_06.bin`) | `gfx_load`, `sound_init`, `load_file`, `harness/common.c` | `RomSet` en memoria (`runtime/romset.c`): el anfitrión da un `RomSource` y el núcleo recibe búferes verificados; `hostio` lo rellena desde carpeta o zip |
| nombres de archivo de un solo juego de nombres (los antiguos) | `gfx.c`, `interleave.py`, `sound.c` | catálogo con CRC y tamaño; el nombre solo sirve para los mensajes (D38) |
| `getenv("SF2_VIDEO_LAYERS")`, `getenv("SF2_VIDEO_PROBE")`, `getenv("SF2_Z80_TRACE")` | `video.c`, `sound.c` | campos (`Video.layer_mask`, `sound_set_trace`) que rellena `sf2_blind` desde el entorno; la sonda de píxel se quitó |
| `fopen` para PNG, WAV, volcados de RAM y secuencia de entradas | `video.c`, `wav.c`, `cps1.c` | `png_encode_rgb` en memoria (mismos bytes: los 17 PNG son idénticos), WAV a `platform/`, volcados a `blind.c`, `cps1_parse_inputs(texto)` |
| `fprintf(stderr, …)` en el núcleo | `mem.c` (registro de E/S), `video.c` (volteo), `sound.c` (pin 7 del OKI) | `Mem.log_file`; banderas que informa el anfitrión |
| `exit()` y variables globales (`g_machine`, `g_resume`, `g_in_fallback`) | `harness/machine.c` | `Machine` sin global (`M68K` primer miembro: los ganchos `rt_*` recuperan la máquina de su puntero), errores por `MACHINE_ERROR` y `Machine.error`; permite dos máquinas en un proceso (las pruebas de estados las usan) |
| orden de bytes: `Z80Pair` accede a `.b.l/.b.h` de un `uint16_t` suponiendo little-endian | `z80.h` | orden de los miembros según `SF2_LITTLE_ENDIAN` (`sf2_port.h`), con prueba del orden en `unit` |
| `pow()` de la libm para la tabla de pasos del OKI | `oki6295.c` | tabla de MAME escrita a mano (49 enteros), comprobada contra `floor(16·1,1^n)` en las pruebas |
| `musttail` en el código generado | `gen_c.py` | `sf2_port.h` falla al compilar sin él: clang ≥ 13 o GCC ≥ 15 (GCC 14 no vale; MSVC no tiene el atributo) |
| **memoria sin inicializar en la cola de sucesos aplazados del YM2151** | `ym2151.cpp` | puesta a cero y normalizada al guardar; ver abajo |
| tipos de tamaño no fijo | todo `runtime/` | auditado: no hay conversiones de punteros de búferes a tipos más anchos (ni `(uint16_t *)` ni similar), `long` solo aparece como `unsigned long long` en `printf`, y los `_Static_assert` de `sf2_port.h` fijan 8 bits por byte, `int` ≥ 32 bits, IEEE-754, complemento a dos y desplazamiento aritmético |
| alineación | todo `runtime/` | lectura y escritura de palabras de 16/32 bits por composición de bytes (`mem.c`), `memcpy` en la serialización: sin accesos alineados que supongan nada |
| rutas | carga de ROMs | no hay `/` fijo ni `\` en el núcleo; `hostio` acepta ambos separadores en los nombres dentro del zip |

### Ejecución por cuadros (D39)

El bucle de SDL y `requestAnimationFrame` necesitan que el núcleo devuelva el control cada cuadro, pero el código recompilado
anida llamadas C: no se puede volver de una instrucción cualquiera. Se hace con `longjmp` y reanudación por la tabla de despacho,
que es lo que ya hace el código al cambiar de tarea (D9), con tres condiciones: dirección en la tabla, fuera de un manejador de
IRQ y tras un VBlank. Al reanudar, el gancho de esa instrucción no se repite (`skip_hook`), así que el contador de instrucciones y
los ciclos no cambian. `sf2_blind --step` lo ejercita:

```
$ build/sf2_blind build/rom.bin --frames 12000 --inputs tools/inputs_sf2.txt --summary build/summary_step.txt --step
--step: 12105 cesiones para 12106 VBlank
OK: 12001 cuadros, 179434358 instrucciones … 2029934136 ciclos estimados
```

El resumen sale con el mismo SHA-256 que la ejecución seguida (`29339a91…`; prueba `summary_run_vs_step`). Casi cada VBlank
tiene su cesión (la primera dirección con etiqueta llega en unas pocas instrucciones); el caso límite, un VBlank sin ninguna
etiqueta durante muchas instrucciones, no ocurre en la partida y se resolvería sin perder nada (se cede en la siguiente).

### Fachada `sf2.h`

`sf2_create(RomSet)`, `sf2_set_input`, `sf2_set_dips`, `sf2_run_frame`, `sf2_frame`, `sf2_audio`, `sf2_state_*`, `sf2_reset`. El
juego, la medida de rendimiento, las pruebas y `sf2_trace` usan solo esto. `sf2_create` calcula el CRC-32 de la imagen de
programa y lo compara con `recomp_rom_crc32`, que `gen_c.py` escribe en el C generado: otras ROMs de programa se rechazan con un
mensaje que dice qué regenerar.

## Verificación

- **Sanitizadores**: build con `-fsanitize=address,undefined` (clang): `sf2_rom_tests` (carga desde zip y carpeta, 4.000 cuadros
  guardando estados cada 37, estados dañados) y `sf2_blind` 12.000 cuadros con vídeo, sonido y `--step` más el juego `sf2` 4.000
  cuadros sin ventana: **ningún aviso de ASan ni de UBSan** (ni siquiera de desplazamientos de enteros con signo en `cpu.h`).
- **Lo que encontró la portabilidad**: el estado guardado desde el navegador falló en el primer intento. Causa: `Interface::queue`
  del YM2151 era un arreglo sin inicializar; en macOS la memoria nueva viene a cero y la comprobación de rango pasaba, en
  wasm la memoria tenía basura de la carga del zip y la validación (`kind` ∈ {0,1}) rechazaba el guardado. Era un fallo real de
  determinismo (UB al leer memoria sin inicializar), no del navegador: ahora va a cero y solo se serializan las `nqueue` entradas
  vivas. El CRC del estado es desde entonces idéntico entre plataformas (D45).
- `core_purity`, `unit` y todo lo anterior en las siete pruebas de `ctest` (ver la entrada de cierre).

## Pendiente

- El visor `sf2_video` y `harness/view.c` desaparecen (el juego `app/` lo sustituye); `make run-video` lanza `build/sf2`.
- Hito 6: eliminar la red de seguridad de Musashi. El juego, sin ella, termina con «el juego llegó a código que el
  recompilador no vio: salto a XXXXXX» (con el `entry` que habría que añadir a `tools/hints.txt`) si se llegara a código no
  visto en una partida distinta de la de validación; no ha ocurrido en 12.000 cuadros.
