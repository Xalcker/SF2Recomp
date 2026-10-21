# Plan: de la ROM de SF2 (CPS1) a código portable

Última actualización: 2026-10-20 (cierre del hito 6: cobertura y robustez; comprobado también en Linux, Windows y wasm)

## Objetivo

Obtener una versión nativa y portable de **Street Fighter II: The World Warrior**
(arcade, placa Capcom CPS1, revisión World 910522) a partir de su programa 68000,
mediante **recompilación estática**: el código máquina del 68000 se traduce una
sola vez a C, se compila de forma nativa y corre sobre una capa de hardware CPS1
reimplementada (vídeo, sonido, entradas). En tiempo de ejecución no hay
intérprete ni JIT de 68000.

El resultado se parece a lo que hace N64Recomp para Nintendo 64: una herramienta
que genera el código y un runtime portable. El jugador aporta sus propias ROMs;
el repositorio **nunca** contiene código, gráficos ni sonido de Capcom.

## Por qué estática y no dinámica

- La CPU original corre a 10 MHz. Cualquier intérprete moderno la emula sobrado,
  así que un recompilador dinámico no aporta velocidad.
- La recompilación estática produce código fuente real que se puede leer,
  modificar, portar y enlazar con cualquier backend (SDL, consola, web).
- El código está íntegramente en ROM (no hay código en RAM, verificado en el
  hito 0), lo que elimina el principal obstáculo de la recompilación estática.

## Arquitectura destino

```
ROMs del usuario (sf2/)
   │
   ▼
tools/            intercalado de ROMs, análisis, informes de cobertura
   │
   ▼
recompiler/       68000 → C: una función C por función del juego,
                  tabla de despacho para saltos indirectos
   │
   ▼
<build>/generated/ código C generado (no se versiona, se regenera en cada árbol de compilación)
   │
   ▼
runtime/          el NÚCLEO, sin llamadas al sistema: contexto de CPU, mapa de
                  memoria CPS1 con E/S y RAM de paleta, máquina (reloj de cuadro,
                  IRQ2, entradas, ejecución por cuadros), vídeo CPS-A/CPS-B-11,
                  sonido (Z80 interpretado, YM2151 con ymfm, OKI MSM6295, mezcla
                  a 48 kHz), ROMs en memoria con verificación por CRC (zip e
                  inflate propios), estados guardados, fachada sf2.h
   │
platform/         archivos (stdio), ROMs desde zip o carpeta, PNG, WAV, configuración
   │
   ▼
app/              el juego "sf2": SDL3 (ventana, teclado y mandos, audio, ritmo
                  por el audio) y la página web (app/web)
   macOS arm64, Linux arm64/x86-64 (Docker), Windows x86-64 (llvm-mingw), Web (Emscripten)

harness/ + third_party/musashi/   validación: paso cerrado contra Musashi y máquina
                                   "a ciegas" (Musashi de reserva solo con --fallback, D46)
tools/sequences/ + playgen.py + coverage.py   14 partidas que recorren el juego, cobertura dinámica y validación de todas contra MAME
tests/                            ctest: unitarias, ROMs y estados, regresiones, pureza del núcleo, mandos
tools/mame_blind.lua + compare_summaries.py   referencia independiente: MAME local (RAM por cuadro)
tools/mame_snap.lua + compare_frames.py + render_dump   referencia independiente: MAME local (píxeles)
tools/mame_audio.lua + compare_audio.py                  referencia independiente: MAME local (sonido por capas y WAV)
tools/mame_sound_cmds.lua + compare_sound_cmds.py        referencia independiente: MAME local (comandos de sonido)
```

## Hitos

### Hito 0: Análisis de la ROM (hecho, 2026-10-19)

Identificar el juego, la revisión y la estructura del programa. Ver
`docs/bitacora/2026-10-19-hito0-analisis-rom.md`.

### Hito 1: Recompilador 68000 → C y arnés de validación (hecho, 2026-10-19)

- Intercalado de los ocho ROMs de programa en una imagen plana de 1 MB
  (`tools/interleave.py`; el cuarto par es solo datos pero el juego los lee).
- Decodificador 68000 completo (`tools/m68k.py`) y desensamblado por descenso
  recursivo desde la tabla de vectores (`tools/analyze.py`), con resolución de
  las tablas de salto de Capcom, del retorno por registro y de los argumentos
  de `trap` (puntos de entrada de las tareas del núcleo multitarea del juego).
- Generador de C (`recompiler/gen_c.py`) con contexto de CPU y banderas
  calcadas de Musashi (`runtime/cpu.h`), accesos a memoria por funciones sobre
  el mapa CPS1 (`runtime/mem.c`) y tabla de despacho para `jmp`/`jsr`
  indirectos y `rts` no estructurados.
- Arnés (`harness/`) que ejecuta instrucción a instrucción el código generado
  y Musashi, cada uno con su RAM, inyectando la IRQ2 cada 166.666 ciclos en
  ambos lados, e informa la primera divergencia con desensamblado.
- Resultado: `make check` corre 600 cuadros (9,3 M instrucciones) sin
  divergencia; también 7.200 cuadros (104,9 M). Cobertura estática: 15,4 % del
  MB es código (20,6 % de los 768 KB con código), 827 de 836 saltos indirectos
  resueltos; cobertura dinámica a 7.200 cuadros: 23,4 % de las instrucciones.
- No incluye vídeo ni sonido. Bitácora: entradas `2026-10-19-hito1-*`.
- Deja para después: punteros a código guardados en RAM que el análisis no ve
  (se corrigen con `tools/hints.txt` cuando el arnés los encuentra),
  rendimiento del código generado, contraste con la traza de MAME.

### Hito 2: Máquina CPS1 mínima (hecho, 2026-10-19)

- Mapa de memoria completo contrastado con MAME 0.289 (`runtime/mem.c`): ROM 1 MB, RAM gráfica,
  RAM de trabajo, zona de E/S con entradas, dipswitches, control y bloqueo de monedas, CPS-A,
  CPS-B-11 (ID 0x0401), latches de sonido; las zonas sin mapear leen 0x0000. Tabla de las
  direcciones que toca el juego en la bitácora y en `build/io_table.md`.
- Planificador (`runtime/cps1.c`, `harness/blind.c`): IRQ2 cada 167.680 ciclos (59,637 Hz, el
  cuadro de MAME) entregada en límites de instrucción; bucle de espera de VBlank identificado
  (planificador 0xA30-0xA5C ↔ tarea 0 en 0xE38, bandera 0xFF020E). El generador emite el modelo
  de ciclos completo de Musashi: el reloj del código generado coincide con Musashi al ciclo.
- Musashi como red de seguridad en `sf2_blind` para direcciones no recompiladas, con registro de
  episodios; no hizo falta ni una vez en la partida completa.
- Validación contra MAME: `tools/mame_blind.lua` produce el mismo resumen por cuadro (registros al
  entrar en la IRQ2, entradas, hashes de RAM de trabajo y gráfica) e inyecta la misma secuencia de
  entradas (`tools/inputs_sf2.txt`) con `manager.machine.ioport`; `tools/compare_summaries.py`
  señala el primer cuadro distinto. `make run-blind`, `make compare-mame`, `make check-blind`.
- Resultado: partida completa a ciegas de 12.000 cuadros (arranque, atract, moneda, start, Ryu,
  combate contra Blanka, continuar, fin de partida, atract) con 11.982 cuadros idénticos a MAME;
  los 18 restantes son rachas aisladas de 1-3 cuadros en que la IRQ corta una tarea larga en otra
  instrucción, seguidas de resincronización (explicadas en la bitácora). Cobertura dinámica 31,3 %.
- Bitácora: entradas `2026-10-19-hito2-*`; decisiones D15-D19.
- Deja para después: reloj exacto al del núcleo 68000 de MAME (prefetch, bus), secuencias de
  entradas que recorran más juego (ganar asaltos, bonus, continuar, otros personajes).

### Hito 3: Vídeo CPS-A / CPS-B-11 (hecho, 2026-10-19)

- ROMs gráficas (`runtime/gfx.c`): las doce ROMs de `sf2/` intercaladas como la región de MAME
  (`ROM_LOAD64_WORD`, CRC comprobados), formato de tiles de 8x8, 16x16 y 32x32 documentado y
  predecodificado a pens, mapeador de bancos STF29 por tabla.
- Renderizador (`runtime/video.c`, paleta en `runtime/mem.c`): copia de paleta al escribir 0x80010A con
  el control de paleta del CPS-B-11, brillo como MAME, tres capas de scroll (64x64 tiles, row scroll de
  SCROLL2), tabla de objetos con doble búfer (copiada al empezar cada VBlank), bloques de sprites, orden
  de capas y máscaras de prioridad del CPS-B-11, prioridad entre sprites (la primera entrada encima).
  Cuadro de 384 x 224 RGB y escritor PNG sin dependencias. Capa de estrellas y volteo de pantalla
  omitidos (SF2 no los usa, D24).
- Máquina compartida (`harness/machine.c`, ganchos `on_vblank`/`on_sample`), visor SDL3 `sf2_video`
  (`make run-video`, 59,637 Hz, teclado opcional) y modo PNG sin ventana (`sf2_blind --png-frames`).
- Validación: `tools/mame_snap.lua` (capturas en el punto de muestra más volcados de RAM gráfica,
  registros, tabla de objetos y paleta), `tools/compare_frames.py` (PNG a PNG con imagen de
  diferencias), `build/render_dump` (renderiza desde los volcados de MAME), `make compare-video`.
- Resultado: 17 cuadros de referencia (test de RAM, aviso, título, selección, Ryu confirmado, siete
  instantes del combate, KO, continuar, fin de partida, atract, demo) idénticos a MAME píxel a píxel,
  desde el runtime completo y desde los volcados; muestreo denso de 137 cuadros (uno cada 100 más los
  18 cuadros que diferían en RAM en el hito 2), 137 idénticos.
- Bitácora: entradas `2026-10-19-hito3-*`; decisiones D21-D26.
- Deja para después: máscaras de prioridad no ejercitadas por SF2 (valen 0 en toda la partida),
  estrellas y volteo, relación de aspecto y pantalla completa del visor (hito 5).

### Hito 4: Sonido (hecho, 2026-10-19)

- Placa de sonido (`runtime/sound.c`): Z80 interpretado propio (`runtime/z80.c`) con el ciclo de cada
  acceso, ROM `sf2_09.bin` con su banco, 2 KiB de RAM, YM2151 sobre ymfm vendorizado de MAME 0.289
  (`third_party/ymfm`, `runtime/ym2151.cpp`), OKI MSM6295 (`runtime/oki6295.c`, `sf2_18` + `sf2_19`), los dos
  latches que escribe el 68000 (gancho en `mem.c`), mezcla a 48 kHz calcada del remuestreador "lofi" de MAME
  y WAV de 16 bits (`runtime/wav.c`). Corre detrás del 68000 con un único reloj en ciclos del Z80 (D29) y el
  orden de sucesos del planificador de MAME (D30).
- `sf2_blind --audio/--audio-dir/--wav/--sound-cmds`, sonido en el visor `sf2_video` (SDL_AudioStream, `make
  run-video`), `build/z80dis`. El Z80 no es un núcleo de terceros sino un intérprete propio (D27); ymfm sí va en
  `third_party/ymfm` (D28).
- Validación en dos niveles. Comandos (D34): `tools/mame_sound_cmds.lua` + `sf2_blind --sound-cmds` +
  `tools/compare_sound_cmds.py`, `make compare-sound-cmds`. Audio por capas (D32, D33): `tools/mame_audio.lua`
  registra desde MAME comandos, accesos del Z80 con su ciclo, salida cruda de cada chip y mezcla del altavoz, y
  con `-wavwrite` su WAV; `tools/compare_audio.py` compara en ese orden y termina con los dos WAV por tramos
  (energía y espectro, tolerancias en D33) y un informe en `build/audio/informe_audio.md`. `make run-audio`,
  `make wav-recomp`, `make mame-audio`, `make compare-audio`.
- Resultado: en la partida de 12.000 cuadros (203 s: arranque, atract, moneda, selección, combate, KO,
  continuar, fin de partida, demo) los 1.391 comandos caen en el mismo cuadro con el mismo valor, las 456.589
  escrituras y 316.409 lecturas del Z80 coinciden en dirección, valor y ciclo, y las muestras del YM2151, del OKI y
  de la mezcla son idénticas bit a bit; el WAV coincide con el de `-wavwrite` de MAME (todas las muestras) y los
  7 tramos pasan los criterios de energía y espectro con diferencia 0. El visor entrega al dispositivo SDL3 la
  misma mezcla (comprobado con el controlador `disk`).
- Bitácora: entradas `2026-10-19-hito4-*` (referencia de MAME, Z80 y mapa, YM2151, OKI, planificación, mezcla,
  comparación y cierre); decisiones D27-D35.
- Deja para después: el riesgo de que un comando del 68000 escrito a menos de ~250 ciclos de una lectura
  del latch caiga al otro lado (reloj exacto del 68000, hito 6), el pin 7 bajo del OKI (SF2 no lo usa),
  otras frecuencias de salida y sincronización del visor con el reloj de audio, que hoy descarta bloques si
  la cola pasa de 150 ms (D35, resuelto en el hito 5 por D41 y probado con CoreAudio) y, opcional, recompilar
  el Z80.

### Hito 5: Portabilidad (hecho, 2026-10-19)

- **CMake** (`CMakeLists.txt` en la raíz y en cada carpeta, D36): recompilador, runtime, juego, arneses y pruebas; el
  recompilador corre como paso de la compilación sobre `SF2_ROM_DIR` (sf2.zip o carpeta, identificadas por CRC) y deja el C en
  el árbol de compilación; en compilaciones cruzadas, un sub-proyecto nativo. El `Makefile` queda como envoltorio. Las huellas
  del Makefile original (resumen de 12.000 cuadros, WAV, 17 PNG, comandos de sonido) coinciden byte a byte, y `check`,
  `compare-mame`, `compare-video`, `compare-sound-cmds` y `compare-audio` pasan desde el build de CMake.
- **Núcleo sin llamadas al sistema** (D37, comprobado en cada `ctest`) y capa de plataforma separada: `platform/` (archivos,
  ROMs desde zip o carpeta, configuración) y `app/` (SDL3: ventana, teclado, mandos, audio). Auditoría de portabilidad: orden de
  bytes, `pow()` de la libm, `getenv`/`fopen`/`exit` en el núcleo, memoria sin inicializar del YM2151 (encontrada desde wasm),
  `_Static_assert` de los supuestos, ASan/UBSan limpios en la partida completa. El núcleo se ejecuta por cuadros con cesión por
  `longjmp` en las direcciones de la tabla de despacho (D39), sin cambiar un solo bit de la partida.
- **Para el usuario**: carga de ROMs desde carpeta o `sf2.zip` con verificación por CRC y mensajes claros (D38), archivo de
  configuración con dipswitches con nombre, escala y proporción de ventana, volumen y remapeo de teclado y mando para dos
  jugadores (D42), estados guardados y cargados desde el juego (D40), frecuencia de salida de audio configurable y ritmo por el
  reloj del audio sin descartar muestras (D41).
- **Plataformas** (D43-D45): macOS nativo arm64; Web con Emscripten instalado en `external/emsdk` (página que pide
  `sf2.zip`, sin ROMs embebidas); Linux arm64 y x86-64 con Docker (`tools/linux-docker.sh`); Windows x86-64 con llvm-mingw
  local, ejecutado con Wine (prueba de humo). En todas, estado de la máquina y vídeo idénticos al de macOS en los 12 puntos de
  una partida de 12.000 cuadros; el audio, bit a bit en arm64 y con 985 de 9,7 millones de muestras a 1 LSB en x86-64 y wasm
  (D33 pasa en los siete tramos).
- **Rendimiento**: en macOS arm64 el núcleo solo hace unos 9.900 cuadros por segundo (166 veces el tiempo real), con vídeo
  1.380 y con vídeo, sonido y ventana 910; en el navegador, 6.700, 1.060 y 510. Ver la bitácora de rendimiento.
- Bitácora: entradas `2026-10-19-hito5-*` (cmake, plataforma y núcleo, configuración y estados, web, otras plataformas, rendimiento
  y cierre); decisiones D36-D45.
- Deja para después: probar en un Windows real, en Firefox y Safari, en un Linux con pantalla y en un iPhone/Android; empaquetado
  (bundle de macOS, instalador, `.deb`); GCC ≥ 15 y MSVC (este último no tiene `musttail`); la capa de estrellas y el volteo de
  pantalla (D24); y todo lo del hito 6.

### Hito 6: Cobertura y robustez (hecho, 2026-10-20)

- **Secuencias que recorren el juego** (`tools/sequences/`, formato con directivas `#@` en su `README.md`, D48): 14 partidas
  grabadas por un jugador con búsqueda sobre el núcleo (`tools/playgen.py` con la fachada como biblioteca, `libsf2py`): los
  ocho personajes hasta el final (11 combates con los cuatro jefes, 3 fases de bonus, final y créditos, vuelta a la atracción),
  con dificultad mínima, máxima y 2, 2 monedas 1 crédito, 2 para empezar y 1 para continuar, juego gratis y demo sin sonido;
  continuar con iniciales en la tabla de récords, fin de partida sin continuar, dos jugadores, retador a mitad de partida, el
  menú de pruebas (lazo cerrado sobre el cursor) y la partida de validación de los hitos 2-5. 603.050 cuadros (2 h 48 min de
  máquina). Cada una anota el CRC del estado final (`playgen.py --verify`). WW no deja cambiar de personaje al continuar.
- **Cobertura dinámica** (`make coverage`, sin red de seguridad): 31.774 de 43.036 instrucciones (73,8 %) y 1.087 de 1.339
  funciones (81,2 %); **0 fallos de despacho, 0 destinos despachados que el análisis no hubiera enumerado, 0 conflictos del
  análisis**. Lo no ejecutado: las herramientas de desarrollo ocultas (2.429 instrucciones, no alcanzables con entradas) y
  ramas que las partidas no provocan.
- **Validación contra MAME** (`make compare-coverage`): las 14 secuencias **idénticas a MAME en todos los cuadros** (resumen
  por cuadro), en los 135.222 comandos de sonido y en las 814 capturas comparadas. Con el reloj de Musashi 6 de las 14
  divergían para siempre; se implementó el **reloj exacto del 68000 de MAME** (D47: costes, reset, entrada a la IRQ con el
  reloj E y punto de muestreo del microcódigo), que deja también la partida de validación idéntica en modo estricto (de 18
  cuadros aislados y 9.386 con otro PC interrumpido a 0 y 0).
- **Correcciones** que encontraron las partidas: cuatro fallos del análisis estático (D49: retorno por registro, tablas que
  se comían código, tarea creada con llamada en cola, rutinas con texto en línea), siete tablas acotadas en `hints.txt` y el
  listado de conflictos como invariante.
- **Robustez**: Musashi deja de ser red por omisión y un salto a código no recompilado detiene el juego con mensaje, origen,
  destino y remedio, `errores.log` y un estado de rescate (D46); ASan+UBSan sobre `ctest`, las 14 secuencias con vídeo,
  sonido, PNG y estados y el juego 110.000 cuadros, sin un aviso en nuestro código (uno en Musashi, parcheado, D52);
  ejecución de 220.000 cuadros (61 min emulados) con la huella física plana en 24,5 MB y 0 fugas; guardar, reiniciar y
  cargar en 586 puntos de las 14 secuencias da el mismo resumen y el mismo WAV.
- **Rendimiento** (macOS arm64, sin cambiar un bit, D51): núcleo 11.537 → 13.761 cuadros/s, con vídeo 1.809 → 3.669, con
  vídeo, sonido y ventana 1.217 → 1.875 (31 veces el tiempo real).
- **Otras plataformas con el código final** (criterio de D45): Linux arm64 y x86-64 (Docker) pasan las 8 pruebas de `ctest`
  tras arreglar el enlace de `libsf2py.so` (núcleo con `-fPIC` fuera de Apple y Windows, sin cambiar nada en macOS); wasm en
  Node, 3 de 3; Windows con Wine, 633 y 7.898 comprobaciones. En las cuatro, el estado y el vídeo de los 12 puntos de la partida
  de 12.000 cuadros son idénticos a macOS. El audio es bit a bit en arm64; en x86-64, wasm y Windows, 985 de 9,7 millones de
  muestras quedan a 1 LSB, el mismo WAV en las tres, y D33 pasa. Las 13 secuencias reproducen su CRC de estado en Linux arm64 y
  x86-64 (591.050 cuadros). Linux arm64 mide 12.763 · 3.258 · 1.741 cuadros/s (núcleo · vídeo · completo).
- Bitácora: entradas `2026-10-19-hito6-*` (secuencias, correcciones, reloj, cobertura, sanitizadores y ejecución larga,
  rendimiento), `2026-10-20-hito6-cierre` y `2026-10-20-hito6-plataformas`; decisiones D46-D52.
- Deja para después: ver «Estado del proyecto» abajo.

## Estado del proyecto al cerrar el hito 6

**Hecho**: el programa 68000 de SF2 World Warrior se traduce a C en la compilación (43.036 instrucciones, 1.339 funciones,
tabla de despacho de 11.325 etiquetas, sin intérprete en el juego) y corre sobre un núcleo portable con vídeo CPS-A/CPS-B-11,
sonido (Z80, YM2151, OKI) y estados guardados, en macOS, Linux, Windows y el navegador. Contra MAME 0.289: 603.050 cuadros de
14 partidas que recorren el juego entero idénticos en RAM, entradas, comandos de sonido y capturas, la partida de validación
idéntica también en registros y ciclo de la IRQ, y el sonido de la partida de validación igual bit a bit. Sin avisos de los
sanitizadores, sin fugas ni crecimiento de memoria, estados que reproducen la partida exacta. Con el código final, Linux arm64 y
x86-64, wasm y Windows (Wine) dan el mismo estado y el mismo vídeo que macOS; el audio es bit a bit en arm64 y pasa D33 en el
resto (entrada `2026-10-20-hito6-plataformas`).

**Queda abierto**:

- El 26 % de las instrucciones no lo ha ejecutado ninguna partida; depende del análisis estático (sin conflictos) y, si fallara,
  el juego lo dice con el remedio (D46). Más partidas con el mismo generador lo reducirían.
- Probar en un Windows real, Firefox, Safari, un Linux con pantalla y móviles; empaquetado (bundle de macOS, instalador,
  `.deb`); GCC ≥ 15 y MSVC (sin `musttail`).
- La capa de estrellas y el volteo de pantalla (D24); DIVU/DIVS con la temporización exacta (el juego no ejecuta su único DIVU).
- Rendimiento opcional: sonido (ymfm, Z80 interpretado), la llamada a `rt_hook` por instrucción, recompilar el Z80.
- Extensiones del plan original: netplay con rollback (los estados exactos y deterministas ya lo permitirían), mods;
  limpieza del código generado para leerlo como fuente.

## Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Destinos de salto indirecto no descubiertos estáticamente | Tabla de despacho, listado de conflictos a cero (D49) y cobertura dinámica: 0 fallos y 0 destinos no enumerados en 603.050 cuadros; si faltara uno, el juego se detiene con el remedio y un estado de rescate (D46) |
| Varias revisiones con distintos chips CPS-B | Fijar una sola revisión objetivo (World 910522, archivos `sf2e.*g`) |
| Dependencias de temporización por ciclo | Hizo falta en partidas largas (6 de 14 divergían con el reloj de Musashi): reloj exacto del 68000 de MAME (D47), 0 cuadros distintos en las 14 secuencias |
| Sonido sensible al ciclo (sondeo del YM2151, lectura del latch en la INT) | Z80 con el ciclo de cada acceso y orden de sucesos de MAME (D27, D30) y reloj exacto del 68000 (D47): comandos de sonido iguales en las 14 secuencias |
| Distribución de contenido de Capcom | Solo se versionan herramientas y runtime; las ROMs quedan fuera del repositorio |

## Convención de documentación

- Cada paso de trabajo deja una entrada en `docs/bitacora/AAAA-MM-DD-hitoN-tema.md`
  con: qué se hizo, comandos ejecutados, resultados, qué quedó pendiente.
- Las decisiones de diseño se numeran en `docs/DECISIONES.md`.
- Cada carpeta de herramienta o módulo lleva su propio `README.md`.
- Este plan se actualiza al cerrar cada hito.
