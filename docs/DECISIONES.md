# Decisiones de diseño

Formato: número, fecha, decisión, motivo. Se añaden al final; no se reescriben.

## D1 (2026-10-19): Recompilación estática, no dinámica

La CPU de 10 MHz no necesita velocidad. El objetivo es obtener código fuente
nativo, portable y modificable, no un emulador más rápido.

## D2 (2026-10-19): Revisión objetivo única: World 910522

Archivos `sf2e.30g`, `sf2e.37g`, `sf2e.31g`, `sf2e.38g`, `sf2e.28g`, `sf2e.35g`.
Las demás revisiones del directorio `sf2/` usan otros chips CPS-B y otro
layout de registros; soportarlas multiplicaría el trabajo de vídeo.

## D3 (2026-10-19): El primer hito no toca hardware

El recompilador se valida solo contra el intérprete Musashi con memoria
compartida. Vídeo y sonido llegan en hitos posteriores sobre una base ya
verificada.

## D4 (2026-10-19): Lenguajes

Código generado en C puro. Runtime en C o C++17. Herramientas de análisis en
Python 3 (sin dependencias externas mientras sea posible).

## D5 (2026-10-19): Las ROMs nunca entran al repositorio

La carpeta `sf2/` va en `.gitignore`. El código generado tampoco se versiona,
porque deriva del programa de Capcom; se regenera a partir de las ROMs del
usuario.

## D6 (2026-10-19): Sonido con Z80 interpretado al principio

Recompilar el Z80 es trabajo extra sin beneficio inmediato. Se deja como
opción para el hito 6.

## D7 (2026-10-19): Emulador de referencia: MAME actual, no un núcleo libretro

Lo que necesitamos del emulador de referencia a partir del hito 2 es trazar la
CPU instrucción a instrucción, guardar y cargar estados, capturar cuadros y
volcar audio, todo de forma automatizable. Solo MAME completo lo ofrece
(depurador con `trace`, scripts Lua, `-snapshot`, `-wavwrite`, `-str`).

- Los núcleos libretro mame2000, mame2003 y mame2003-plus no tienen depurador
  ni Lua y solo corren dentro de RetroArch. Además mame2000 (MAME 0.37b5) usa
  otra revisión del juego (910214) y un vídeo CPS1 menos exacto.
- El código fuente de mame2003 (`src/drivers/cps1.c` y `src/vidhrdw/cps1.c`,
  en C) sí se usa como lectura de apoyo: es más corto que el C++ actual y sus
  nombres de archivo coinciden con nuestro set.
- Nuestro set es de la época de MAME 0.78. MAME actual exige además siete
  volcados de PLD (`buf1`, `ioa1`, `prg1`, `rom1`, `sou1`, `stf29.1a`,
  `iob1.11d`, `c632.ic1`) y se niega a arrancar si faltan (comprobado en
  `src/emu/romload.cpp`: un archivo con volcado conocido ausente cuenta como
  error fatal). Resuelto el mismo día: el usuario aportó un `sf2.zip` completo
  con nombres actuales y los PLD (ver bitácora del hito 0, limpieza).
- Hecho el mismo día: MAME 0.289 compilado dentro del proyecto como build
  mínimo solo con el driver CPS1 (`external/mame/cps1`, 83 MB), con depurador
  y Lua. Se lanza con `tools/mame.sh`. No se instala nada fuera del proyecto.
  Ver bitácora "MAME compilado dentro del proyecto".

## D8 (2026-10-19): Banderas del 68000 como bytes independientes, con la semántica exacta de Musashi

En el runtime (`runtime/cpu.h`) X, N, Z, V y C son cinco bytes 0/1 y el SR se
compone solo cuando hace falta. Cada operación las calcula de forma simple y
explícita, copiando también los comportamientos "indefinidos" que Musashi
implementa (V y N de ABCD/SBCD/NBCD, V de ASL, DIV con desbordamiento, CHK,
desplazamientos con cuenta mayor que el tamaño, ROX módulo 9/17/33). Motivo:
el arnés compara el SR instrucción a instrucción; cualquier atajo "correcto
según el manual" pero distinto de Musashi aparecería como divergencia. La
velocidad llega después (hito 6).

## D9 (2026-10-19): Modelo de llamadas del código generado y `rts` no estructurados

`jsr` empuja la dirección de retorno en la pila emulada y hace una llamada C
anidada; `rts` saca la dirección a `c->pc` y retorna; el llamador comprueba que
coincide con la esperada y, si no, retorna a su vez. El desenrollado termina
en el nivel cuyo retorno coincide o en el trampolín, que busca `c->pc` en la
tabla de despacho y entra en la función propietaria por su `switch` de
etiquetas. `jmp` entre funciones es una llamada en cola garantizada
(`musttail`). Motivo: el juego cambia de tarea, descarta marcos y salta con
`pea X ; rts`; este esquema mantiene la pila C alineada con la del 68000 en el
caso normal y se resincroniza solo en los demás, sin heurísticas.

## D10 (2026-10-19): El arnés usa memorias separadas y el recompilado lleva la iniciativa

Matiza D3 ("memoria compartida"): cada lado tiene su propia RAM porque una
instrucción read-modify-write sobre memoria compartida contaminaría al otro
lado. La ROM se comparte. El código recompilado llama a un gancho antes de
cada instrucción; el gancho entrega la IRQ en ambos lados, compara registros y
escrituras y avanza Musashi una instrucción. El reloj de la IRQ2 (166.666
ciclos) lo marcan los ciclos de Musashi. La RAM completa se compara cada 60
cuadros.

## D11 (2026-10-19): Los bloques alcanzables desde varias entradas se duplican por función

El cuerpo de una función es todo lo alcanzable desde su entrada sin atravesar
otra entrada; un bloque compartido (epílogos comunes, colas de dispatch) se
emite en cada función que lo alcanza (17.895 de 43.216 instrucciones). La tabla
de despacho apunta a la función "propietaria" de cada etiqueta (la de entrada
más cercana por debajo). Motivo: simplicidad y `goto` locales; el coste es
tamaño de C, no corrección.

## D12 (2026-10-19): Heurísticas de análisis acotadas, con correcciones manuales en `tools/hints.txt`

El fin de una tabla de salto se decide por heurísticas (primer destino tras la
tabla, bases de otras tablas, etiquetas directas, 1024 entradas) en tres
rondas de análisis. Lo que no se resuelva estáticamente lo detecta el arnés en
ejecución y se corrige con `entry`, `label` o `table_end` en `hints.txt`, que
el generador relee. Motivo: preferimos un análisis conservador y verificable a
heurísticas agresivas que tomen datos por código.

## D13 (2026-10-19): Musashi vive en `third_party/` configurado como 68000 puro

`m68kconf.h` desactiva 68010/020/030/040 y la PMMU, usa acuse de interrupción
por callback y no emula prefetch ni error de dirección (el runtime tampoco).
Musashi solo se enlaza en `harness/` y en las herramientas; el ejecutable del
juego no lo lleva. La versión incluida es la 4.60 (commit 313ebf1 del repo de
Karl Stenerud, licencia MIT en `third_party/musashi/readme.txt`).

## D14 (2026-10-19): El cuarto par de ROMs se carga aunque no contenga código

El programa ocupa 1 MB (cuatro pares; D2 nombra los seis chips con código).
El cuarto segmento (0xC0000-0xFFFFF) no tiene código pero el juego lee datos de
él, así que forma parte de la imagen. Las direcciones sin mapear (incluida la
ROM no poblada hasta 0x3FFFFF) devuelven 0xFFFF hasta que se contraste con
MAME en el hito 2.

## D15 (2026-10-19): El cuadro dura 167.680 ciclos de CPU, el de MAME, no 1/60 s

La pantalla CPS1 va a 8 MHz de píxel con 512 × 262 ciclos por cuadro (59,637 Hz; `cps1.h` de
MAME) y la CPU a 10 MHz. El hito 1 usaba 166.666 ciclos. El reloj del runtime y del arnés toman
el valor de MAME (`CPS1_FRAME_CYCLES`) para que las IRQ caigan en los mismos puntos del código al
comparar; `--frame-cycles` permite cambiarlo. Las cifras del hito 1 (493 IRQ en 600 cuadros)
cambian ligeramente por ello.

## D16 (2026-10-19): El código generado lleva el modelo de ciclos de Musashi completo

Además de la base por opcode, el generador emite los ajustes dependientes de los datos de Musashi
para el 68000 (Bcc no tomado, DBcc, Scc, cuenta de los desplazamientos, registros de MOVEM, bits
de MULU/MULS, coste total de las excepciones y 44 ciclos por IRQ). Motivo: con solo la base, el
arnés a ciegas se desfasaba un cuadro respecto a MAME en las tareas de carga que cruzan varios
VBlank y la comparación dejaba de servir; con el modelo completo el reloj coincide con Musashi al
ciclo (`--cycle-check`: 0 diferencias en 31,6 millones de instrucciones). No se persigue el reloj
exacto del núcleo de MAME 0.289 (prefetch, bus): los cuadros aislados que aún difieren se aceptan
y se documentan.

## D17 (2026-10-19): La referencia por cuadro frente a MAME es un resumen tomado en el manejador de IRQ2, sin las zonas volátiles

El cuadro es la entrega de la IRQ2 y la muestra se toma en la instrucción 0xAD0 del manejador (su
primera lectura de E/S, 0x800172), que en MAME se intercepta con un tap de lectura filtrado por
`CURPC`. Se comparan la RAM de trabajo (hash FNV-1a por palabras) sin los bloques de tarea, las
variables del planificador y las pilas de usuario (0xFF0000-0xFF0A4F) ni la pila supervisora
(0xFFFF00-0xFFFFFF), la RAM gráfica completa, las entradas leídas y el contador de cuadros del
juego. Motivo: esas zonas dependen del punto exacto del bucle de espera en que cae la IRQ, que no
es el mismo con relojes distintos, mientras que el resto de la RAM solo cambia cuando el juego
trabaja. El PC interrumpido y los registros se registran como información, no como criterio. Se
descartó comparar trazas instrucción a instrucción con MAME: su reloj difiere del nuestro en cada
cuadro ocioso y el volumen (15 millones de instrucciones por minuto) no aporta más que el hash.

## D18 (2026-10-19): Las entradas se inyectan por cuadro con la misma tabla en los dos lados y en MAME con `set_value` un cuadro antes

`tools/inputs_sf2.txt` (`desde hasta tecla`, cuadros inclusive) la leen `runtime/cps1.c` y
`tools/mame_blind.lua`. El runtime aplica las entradas del cuadro N al entregar su IRQ2; MAME
actualiza los puertos al empezar cada VBlank, justo antes de la IRQ2, así que el script llama a
`field:set_value()` durante la muestra del cuadro N-1 (comprobado: un `set_value` en el cuadro N se
lee en N+1). El bloqueo de monedas de MAME (`coin_lockout`) se imita en el runtime en vez de
desactivarlo, para no cambiar el comportamiento de referencia. Se descartó sustituir las lecturas
de los puertos con un tap porque el encargo pide usar `manager.machine.ioport` y porque el
resultado observado (entradas leídas iguales en todos los cuadros) ya es el deseado.

## D19 (2026-10-19): Musashi es red de seguridad en el arnés a ciegas, no en el ejecutable final

`sf2_blind` enlaza Musashi y, cuando el PC llega a una dirección sin función recompilada,
interpreta sobre la misma memoria hasta volver a una conocida, registrando entrada, salida e
instrucciones de cada episodio. Esto evita que una partida se corte por un puntero a código no
visto por el análisis y da directamente los `entry` que faltan en `tools/hints.txt`. El objetivo
del hito 6 sigue siendo eliminar el fallback (D13).

## D20 (2026-10-19): Las ejecuciones de referencia de MAME corren sin dispositivos de entrada del anfitrión

`make compare-mame` lanza MAME con `-keyboardprovider none -mouseprovider none -joystickprovider
none -lightgunprovider none` además de `-video none -sound none -nothrottle` (`MAME_HEADLESS` en el
Makefile). Motivo: sin ventana, el OSD de MAME sigue registrando el teclado, el ratón y el puntero
del sistema (`-verbose`: "Input: Adding keyboard #1: System keyboard") y en macOS el proceso recién
lanzado recibe el foco, así que una tecla pulsada durante la ejecución llega al juego; en una
verificación independiente una flecha izquierda se leyó como P1 Left (IN1 = 0xFFFD) en los cuadros
192-197 y la partida divergió. Con los proveedores a `none` las únicas entradas son las de la
tabla. Se descartó `-record`/`-playback` porque añade un archivo intermedio y no impide la entrada
espuria durante la grabación. El comparador trata cualquier diferencia del campo de entradas como
error, nunca como racha aislada de temporización.

## D21 (2026-10-19): El vídeo se calca de `cps1_v.cpp` de MAME 0.289, peculiaridades incluidas

El objetivo del hito 3 es coincidir píxel a píxel con el MAME local, así que `runtime/video.c` y
`runtime/gfx.c` reproducen el modelo de MAME y no una lectura "ideal" del hardware: copia de la paleta
en cada escritura de la base de paleta con las reglas de salto de páginas del control de paleta;
fórmula de brillo `0x0F + 2·b` sobre `0x2D` con división entera; tabla de objetos copiada al empezar el
VBlank y usada un cuadro después; mapeador de bancos por tabla (`mapper_STF29_table`); tiles fuera de
rango transparentes; pen 15 transparente; el pen 15 de un tile también marca prioridad si su máscara
tiene el bit 15; y la regla de `prio_transpen` con el bit 31 implícito (un sprite nunca sobrescribe a
otro ya dibujado: la primera entrada de la tabla queda encima). Donde MAME lee fuera de su RAM gráfica
(bases altas) el runtime lee 0. Motivo: cualquier atajo aparece como diferencia en la comparación, y
las peculiaridades de MAME están contrastadas con placas reales según sus comentarios.

## D22 (2026-10-19): El cuadro se renderiza al cruzar el umbral de ciclos del VBlank, antes de la IRQ2

`harness/machine.c` llama a `on_vblank` cuando el reloj estimado cruza el múltiplo de 167.680 ciclos,
antes de marcar la IRQ2 pendiente, y ahí `video_vblank` renderiza con los registros vigentes (los del
manejador anterior) y después copia la tabla de objetos. Es el orden de `vblank_begin` de MAME
(`frame_update`, llamadas de VBlank, IRQ). El PNG del cuadro N se escribe en el punto de muestra 0xAD0
del manejador N, el mismo instante en que `tools/mame_snap.lua` llama a `screen:snapshot()`, que
devuelve el cuadro renderizado en ese VBlank. Durante el test de RAM (IRQ enmascarada) se sigue
renderizando y latcheando en cada VBlank, como MAME. Se descartó renderizar en la entrega de la IRQ
porque con la IRQ enmascarada la copia de sprites no se haría en cada VBlank.

## D23 (2026-10-19): ROMs gráficas intercaladas en memoria como la región de MAME y predecodificadas a pens

`gfx_load` reconstruye la región "gfx" de 6 MiB de `ROM_LOAD64_WORD` (palabra de cada ROM cada 8
bytes) y la decodifica entera a un byte por píxel (12 MiB) en unidades de 16 píxeles, de modo que el
renderizador solo indexa filas de tile. Motivo: el mapeador y los códigos de tile de MAME se expresan
sobre esa región, así que la tabla STF29 se copia sin traducir; el coste de memoria es irrelevante y el
renderizado queda en ~0,15 ms por cuadro sin caché de tilemaps. Nada de esto se escribe en disco (D5).

## D24 (2026-10-19): Capa de estrellas y volteo de pantalla fuera del alcance

SF2 no tiene ROM de estrellas ni habilita esas capas, y el volteo (bit 15 del control de vídeo) solo
se activa con un dipswitch que la referencia no usa. No se implementan; el renderizador avisa una vez
si el juego activara el volteo. Se documenta como omisión deliberada en lugar de añadir código sin
forma de validarlo contra MAME.

## D25 (2026-10-19): Comparación de cuadros sin dependencias externas

`tools/compare_frames.py` lee y escribe PNG con `zlib` de la biblioteca estándar y `runtime/video.c`
escribe PNG con bloques zlib sin comprimir, CRC-32 y Adler-32 propios. Motivo: D4 (herramientas sin
dependencias mientras sea posible) y portabilidad del runtime (hito 5); el coste es que los PNG del
runtime ocupan 258 KB en vez de ~40 KB.

## D26 (2026-10-19): La referencia de vídeo son capturas de MAME en el punto de muestra, más volcados para aislar el renderizador

`tools/mame_snap.lua` guarda la captura dentro del tap del cuadro (la misma numeración que el resumen
del hito 2) y vuelca RAM gráfica, registros, tabla de objetos usada y paleta, los registros y la tabla
leídos en `register_frame_done` (después de renderizar, antes de copiar sprites y de la IRQ).
`build/render_dump` renderiza desde esos volcados: si coincide con la captura, el renderizador es
correcto y una diferencia del runtime completo es de estado o temporización. Se eligió esto frente a
comparar solo la salida del runtime porque separa las dos causas en una sola pasada; y frente a
capturar en `frame_done` porque la numeración por cuadro de IRQ2 es la que comparten el resumen del
hito 2 y la secuencia de entradas.

## D27 (2026-10-19): Z80 interpretado propio, con el ciclo de cada acceso

`runtime/z80.c` es un intérprete escrito para el proyecto (no el núcleo de MAME ni una biblioteca), que
pasa a cada acceso al bus el ciclo T del comienzo de su ciclo M. Motivo: el programa de sonido sondea el
YM2151 y lee el latch en momentos concretos, y la única forma de reproducir lo que lee es saber en qué
ciclo lo lee; un intérprete que solo cuenta ciclos por instrucción se desplaza unos ciclos en cada
acceso. Propio para que la coincidencia con MAME sea una comprobación y no una copia (la descripción de
`z80.lst` solo se consultó para confirmar los ciclos internos de las instrucciones con desplazamiento).
Recompilarlo (D6) sigue siendo opcional: va a unas 50 veces tiempo real.

Alternativa considerada y no adoptada: el núcleo `z80.h` de floooh/chips (licencia zlib), que el encargo del
hito proponía como componente permisivo en `third_party/`. No se probó: el criterio de aceptación es el ciclo de
cada acceso frente a MAME (D32), y un núcleo propio ya lo cumple en toda la partida (456.589 escrituras y 316.409
lecturas iguales al ciclo); evita además un componente más que mantener y portar (hito 5). Cambiar de núcleo
más adelante es posible sin tocar el resto: la interfaz es `Z80Bus` de `runtime/z80.h` y la prueba es
`make compare-audio`.

## D28 (2026-10-19): YM2151 con ymfm vendorizado de MAME 0.289 y la interfaz de tiempo de `ymfm_mame.h`

`third_party/ymfm` es una copia de los cinco archivos de ymfm que usa el YM2151 en el árbol de MAME 0.289
(BSD-3). El envoltorio C++ de `runtime/ym2151.cpp` implementa `ymfm_interface` como el dispositivo de MAME:
ocupado = 64 ciclos desde la escritura, temporizadores de una vez, escritura del registro de modo y
recálculo de la IRQ aplazados tras el acceso. Motivo: el plan pedía ymfm, y con la misma versión que la
referencia la síntesis da los mismos enteros; lo que hay que acertar es solo el tiempo.

## D29 (2026-10-19): La placa de sonido corre detrás del 68000 con un único reloj en ciclos del Z80

El 68000 deja sus escrituras en los latches con su ciclo (gancho en `mem.c`) y la placa avanza hasta el
ciclo del 68000 en cada VBlank (`sound_run_until`). El tiempo se cuenta en ciclos del Z80 desde el
arranque, y el ciclo c del 68000 es floor(c · 3579545 / 10⁷). El latch cambia desde el ciclo del final de
la instrucción del 68000. Motivo: en la CPS1 el Z80 no puede afectar al 68000, así que no hace falta
intercalarlos instrucción a instrucción; correr detrás es exacto y no toca el código generado. El YM2151
comparte el cristal del Z80, así que no hay conversiones de reloj dentro de la placa.

## D30 (2026-10-19): Se reproduce el orden de sucesos del planificador de MAME, no uno "físico"

Reglas adoptadas, cada una comprobada contra MAME (bitácora `2026-10-19-hito4-planificacion.md`): lo que
ymfm aplaza con `synchronize()` se aplica en el ciclo del acceso, después de él (dentro de un callback
`machine().time()` es el vencimiento del temporizador); los temporizadores del YM vencen en ciclos enteros y
la INT se acepta en el primer límite de instrucción con ciclo ≥ vencimiento; la actualización periódica k
(k / 50 s) cuenta como hecha antes de cualquier acceso desde floor(k · 71.590,9); antes de cada acceso a un
chip se generan sus muestras hasta floor(t · f) inclusive. Motivo: el criterio es igualdad con MAME y
cada una de estas reglas cambia muestras concretas; otra elección razonable (por ejemplo aplicar la
escritura del modo al final del ciclo M) da accesos 7 ciclos tarde o una muestra distinta.

## D31 (2026-10-19): La mezcla calca el remuestreador "lofi" de MAME, incluida la aritmética en float

`runtime/sound.c` mezcla a 48 kHz con el algoritmo de `audio_resampler_lofi::apply`, bloques de la
actualización de 50 Hz, ganancias 0,35/0,35/0,30 en el orden de rutas de MAME y la conversión a 16 bits de
`output_push`. Las expresiones se escriben igual que en MAME y se compilan con la contracción a FMA por
defecto de clang (la que tiene el binario de MAME en arm64), salvo la tabla de interpolación, que MAME
evalúa al compilar y por tanto sin FMA (`#pragma STDC FP_CONTRACT OFF`). Motivo: así la salida del runtime
es el WAV de MAME byte a byte y cualquier diferencia futura es un error, no ruido de redondeo. Coste: la
igualdad bit a bit depende del compilador y de la arquitectura (en x86-64 sin FMA el resultado del binario
de MAME también sería otro); en otras plataformas el sonido es el mismo salvo en el último bit.

## D32 (2026-10-19): La referencia de sonido se toma por capas: sucesos al ciclo, flujos de cada chip y mezcla

`tools/mame_audio.lua` registra los comandos del 68000, cada acceso del Z80 a la E/S de sonido con su
ciclo y valor (taps), la salida cruda del YM2151 y del OKI y la entrada del altavoz (gancho
`emu.register_sound_update` con `hook = true`). `tools/compare_audio.py` compara en ese orden. Motivo:
comparar solo el WAV mezcla tres fuentes de error (Z80, chips, remuestreo); con las capas, la primera
diferencia apunta a su causa (así se encontraron las tres de la bitácora). El WAV de `-wavwrite` se usa
como comprobación final independiente del gancho Lua.

## D33 (2026-10-19): Los WAV se comparan por energía y espectro por tramos, con tolerancias fijas, además de la igualdad bit a bit

`tools/compare_audio.py` compara el WAV del runtime con el de `-wavwrite` de MAME, por tramos de la partida, con
tres criterios: diferencia de energía RMS ≤ 0,5 dB (un tramo bajo −80 dBFS es silencio), diferencia de energía por
banda de octava ≤ 1,0 dB en las bandas a menos de 60 dB de la más fuerte y distancia espectral logarítmica ≤ 1,0 dB
(ventanas de 2048 muestras, Hann). La igualdad muestra a muestra, el máximo en LSB y la relación señal/ruido se
informan sin exigirlas. Motivo: la igualdad bit a bit que se consigue hoy (D31) depende del compilador y de la
arquitectura (FMA) y se perderá con otra frecuencia de salida o remuestreador (hito 5); el criterio por energía y
espectro sigue sirviendo entonces. Las tolerancias salen de probar alteraciones conocidas (ganancia −1,4 dB, paso
bajo y desplazamiento de 0,5 s fallan; ±1 LSB y una muestra de desfase pasan; bitácora
`2026-10-19-hito4-comparacion-y-cierre.md`). El umbral de silencio evita que un lado con ceros exactos y otro con
±1 LSB suspendan un tramo sin sonido. Sin dependencias (FFT propia en Python), como D25.

## D34 (2026-10-19): La validación del sonido tiene dos niveles con objetivos propios: comandos (`compare-sound-cmds`) y audio (`compare-audio`)

El flujo de comandos del 68000 se registra y compara por separado, por cuadro y sin las ROMs de sonido
(`tools/mame_sound_cmds.lua`, `sf2_blind --sound-cmds`, `tools/compare_sound_cmds.py`), aunque `mame_audio.lua` ya
registra las mismas escrituras con su ciclo. Motivo: es la parte que sigue siendo verificable cuando cambian el Z80,
los chips o la mezcla, es rápida (los 12.000 cuadros en ~15 s con MAME) y su primera diferencia apunta a un cambio
del 68000 o de la máquina y no del sonido. El cuadro es el último punto de muestra pasado, la numeración que
comparten `mame_blind.lua`, `mame_snap.lua` y `mame_audio.lua`; los comandos del último cuadro no se comparan
porque cada lado para en un instante distinto de él. `make mame-audio` graba además el WAV propio de MAME con
`-wavwrite` en la misma pasada que los registros.

## D35 (2026-10-19): En el visor el ritmo del audio lo marca el vídeo y lo que no cabe en la cola se descarta

`sf2_video` entrega cada bloque de 48 kHz a un `SDL_AudioStream` al terminar cada VBlank y descarta el bloque si la
cola pasa de 150 ms. Es lo más simple que no necesita un reloj de audio, y se ha comprobado (controlador `disk` de
SDL3) que lo que se entrega es la mezcla validada, bit a bit y en orden. Con los controladores `disk` y `dummy`,
que no tienen reloj de hardware, descarta ~6 % de los bloques; con un dispositivo real y un reloj de audio distinto
del del anfitrión la cola crecería igual hasta el umbral. No se ha probado con un dispositivo real. Sincronizar con
el audio (esperas del cuadro según la cola o remuestreo adaptativo) es trabajo del hito 5, junto a la frecuencia de
salida configurable.


## D36 (2026-10-19): CMake es el sistema de construcción; el Makefile queda como envoltorio y el recompilador corre como paso de la compilación

`CMakeLists.txt` en la raíz y en `third_party/`, `runtime/`, `platform/`, `harness/`, `tools/`, `app/` y `tests/`.
`SF2_ROM_DIR` (el `sf2.zip` de MAME o una carpeta con los archivos sueltos) es lo único que hay que indicar: CMake
encadena `tools/interleave.py` → `dump_cycles` (tabla de ciclos de Musashi) → `recompiler/gen_c.py` como reglas con
dependencias, deja el C en `<build>/generated` (nunca en el árbol de fuentes, D5) y lo compila en `sf2_recomp`. En
compilaciones cruzadas (Emscripten, mingw) las herramientas tienen que ejecutarse en la máquina que compila, así que
la generación va en un sub-proyecto nativo (`recompiler/host`, lanzado con `ExternalProject`); también se puede dar el C
ya generado con `SF2_GENERATED_DIR`. El `Makefile` conserva los objetivos y variables de los hitos 1 a 4 (`check`,
`compare-mame`, `compare-video`, `compare-sound-cmds`, `compare-audio`, `BFRAMES`, `VFRAMES`...) pero ya no compila:
configura `build/` y construye el objetivo del mismo nombre de CMake. Motivo: el encargo pedía CMake y mantener dos
descripciones de la compilación habría dejado que se desviaran; en lugar de fiarse de que "dan lo mismo", se
comprobó con las huellas del Makefile original (resumen de 12.000 cuadros, WAV, 17 PNG, lista de comandos de sonido):
idénticas byte a byte. CMake no estaba instalado: se instala dentro del proyecto (`external/cmake`, binario oficial de
Kitware con SHA-256 comprobado), como MAME y Emscripten.

## D37 (2026-10-19): El núcleo no llama al sistema; los archivos, la ventana, el audio y las entradas están en `platform/` y `app/`

Núcleo = `runtime/` (CPU recompilada, máquina, vídeo, sonido, ROMs en memoria, estados). Regla: solo biblioteca
estándar (memoria dinámica, `string.h`, `setjmp.h`, `math.h` en las pruebas) y los `FILE *` que le da el anfitrión para
los registros de validación; ni `fopen`, ni `getenv`, ni `exit`, ni relojes, ni hilos, ni SDL. Lo que antes lo
incumplía se movió o se reescribió: carga de ROMs por carpeta (ahora `RomSet` en memoria, `platform/hostio.c` la rellena),
`getenv` de depuración de vídeo y Z80 (ahora campos que ponen los arneses), escritura de PNG (`runtime/png.c` codifica
en memoria), WAV (`platform/wav.c`), volcados de RAM (`harness/blind.c`), mensajes por `stderr` (`Mem.log_file`),
`exit` en errores fatales (`rt_fatal` deja el mensaje en `Machine.error` y vuelve a quien llamó). Se comprueba en cada
`ctest` (`core_purity`): `nm -u` sobre las bibliotecas del núcleo y rechazo de una lista de símbolos del sistema; probado
en negativo con `platform/`, que sí los usa. Además `runtime/sf2_port.h` fija con `_Static_assert` lo que el código supone
(bytes de 8 bits, `int` de 32, IEEE-754, complemento a dos y desplazamiento aritmético, orden de bytes del par de
registros del Z80, compilador con `musttail`) y la tabla de pasos del OKI deja de depender de que `pow()` de cada libm
redondee igual (es la tabla de MAME escrita a mano, comprobada contra `pow` en las pruebas).

## D38 (2026-10-19): Las ROMs se identifican por CRC-32 y tamaño, no por nombre, con lector de zip e inflate propios

El mismo contenido circula con los nombres antiguos (`sf2e.30g`, `sf2_05.bin`) y con los actuales del set de MAME
(`sf2e_30g.11e`, `sf2-1m.3a`), y `sf2.zip` los trae con los actuales y 8 volcados de PLD que no se usan. `runtime/romset.c`
lleva el catálogo de las 23 ROMs (CRC, tamaño, destino) y carga lo que encuentre por CRC dentro de un origen abstracto
(`RomSource`: zip en memoria, carpeta, lo que dé el navegador). Faltan, sobran o están mal: informe en español con el
nombre esperado, el CRC y, si un archivo tiene el nombre pero otro CRC, el CRC que tiene. El zip se lee con `runtime/zipread.c`
(directorio central, "stored" y "deflate", todos los desplazamientos validados, CRC del directorio central
comprobado al extraer) y un inflate propio de ~150 líneas, probado con tres vectores de zlib, datos corruptos y 2.000
entradas basura. Alternativa descartada: zlib o miniz como dependencia. El proyecto pide SDL3 como única dependencia
externa de plataforma; un inflate puro, sin estado global y con las pruebas dichas es menos riesgo que otra biblioteca
que portar. El código generado lleva el CRC de la imagen de programa con la que se generó (`recomp_rom_crc32`) y el
juego se niega a arrancar con otras ROMs de programa (otra revisión).

## D39 (2026-10-19): La máquina se ejecuta por cuadros con cesión por `longjmp`, solo en direcciones de la tabla de despacho

El bucle de SDL y `requestAnimationFrame` necesitan que el núcleo devuelva el control cada cuadro, pero el código
recompilado anida llamadas C (un `jsr` es una llamada) y no se puede volver de ahí sin descartar la pila. Se hace con
el mismo mecanismo con el que el código ya cambia de tarea (D9): `longjmp` hasta `machine_step` y reanudar por
`dispatch_fn(c->pc)`. Restricciones que salen de eso: (1) una instrucción cualquiera no tiene etiqueta en su
función, así que solo se cede en una dirección de la tabla (`rt_lookup(pc) != NULL`); (2) no dentro del manejador de
IRQ, cuyo `rte` tiene que volver al marco C de la instrucción interrumpida; (3) después de haber pasado un VBlank. Al
reanudar, el gancho de esa instrucción no se repite (`skip_hook`), así que el contador de instrucciones y los
ciclos no cambian. Alternativas descartadas: Asyncify o JSPI (instrumentar o suspender todo el código generado, cada
una con su coste y sus navegadores), un hilo por máquina (pthreads en la web exigen cabeceras de aislamiento). Resultado:
`sf2_blind --step` produce el mismo resumen de 12.000 cuadros que la ejecución seguida (hash SHA-256 idéntico, prueba
`summary_run_vs_step` de ctest) con 12.105 cesiones para 12.106 VBlank. El caso límite (un bucle de muchas
instrucciones sin ninguna etiqueta tras un VBlank) no ocurre en la partida de validación; si ocurriera, la máquina
sigue y cede en la siguiente dirección con etiqueta, sin perder cuadros.

## D40 (2026-10-19): Estados guardados: un solo recorrido de campos para guardar y cargar, little-endian, validados, de carga atómica

`runtime/state.h` (`StateStream`) escribe o lee según el modo, y cada módulo tiene una única función de estado
(`mem_state`, `z80_state`, `ym2151_state`, `sound_state`, `machine_state`...), así que no pueden desincronizarse; los
`float` van como su patrón de bits. Formato: cabecera de 24 bytes (marca `SF2R`, versión, CRC-32 del código recompilado,
longitud, CRC-32 del contenido) y contenido de ~460 KiB. Un estado viene de un archivo y no es de fiar: se comprueban marca,
versión, CRC del código (un estado de otras ROMs o de otra compilación se rechaza con un mensaje que lo dice), longitud,
CRC del contenido y cada campo que luego sirve de índice o de bandera (`irq_pending` ≤ 7, banderas 0/1, cola de latches,
voces del OKI, PC en la tabla de despacho...). La carga es atómica: antes de tocar nada se guarda el estado actual y, si el
contenido resulta inválido, se restaura. Lo que no se guarda: configuración del anfitrión (dipswitches, entradas pulsadas,
secuencia de entradas), ROMs y cuadro de vídeo (se vuelve a renderizar al cargar). Esto encontró un fallo real: la cola de
sucesos aplazados del YM2151 (`ym2151.cpp`) tenía las entradas sin usar sin inicializar, de modo que el mismo momento de
la partida daba estados distintos según lo que hubiese en la memoria (en el navegador falló el guardado al primer
intento); ahora van a cero y el CRC del estado es idéntico entre plataformas.

## D41 (2026-10-19): El ritmo del visor lo marca la cola de audio, y la frecuencia de salida es la del dispositivo

Resuelve el pendiente de D35. El juego emula cuadros mientras la cola de audio tiene menos de 60 ms (como mucho 4 por
vuelta del bucle), así que corre a la velocidad exacta del reloj de la tarjeta de sonido sin descartar ni repetir
muestras; sin audio, un reloj de 59,637 Hz; con la tecla de avance rápido o `--nothrottle`, sin esperas. La mezcla sigue
siendo la de 48 kHz validada contra MAME: el dispositivo se abre a `[audio] rate` (por omisión 48000) y SDL convierte
con un flujo `SDL_AudioStream` de 48 kHz mono, de modo que cambiar la frecuencia no toca la mezcla. El volumen es la
ganancia del flujo de SDL (nunca se multiplica la mezcla del núcleo). Comprobado con el CoreAudio real de esta máquina: cola
entre 17 y 78 ms durante 10 s de partida, 0 huecos. Alternativa descartada: remuestreo adaptativo del vídeo; no hace
falta cuando el audio manda.

## D42 (2026-10-19): Configuración en un INI con nombres de SDL, dipswitches con nombre y remapeo para dos jugadores

`sf2recomp.ini` (secciones `[video] [audio] [dip] [paths] [keys] [pad] [hotkeys]`) se crea con sus valores por
omisión la primera vez para que se pueda editar; los valores no válidos avisan y se conserva el anterior. Teclas y
botones se guardan como los nombres de SDL (`Up`, `Left Shift`, `rightshoulder`, `righttrigger`), hasta cuatro por acción, y
la capa SDL los resuelve al arrancar (`input_check_config` los valida y una prueba comprueba todos los de omisión).
Los dipswitches son los de `INPUT_PORTS_START( sf2 )` de MAME con nombre (`difficulty` 0 a 7 con 3 = normal, `coin_a`/`coin_b` de
`4c1c` a `1c6c`, `free_play`, `two_coins_to_start`, `demo_sounds`, `allow_continue`, `freeze`, `test_mode`); sus valores por
omisión dan 0xFF/0xFC/0x9F, los de MAME (probado). `flip_screen` no se ofrece porque el vídeo no lo implementa (D24).
Mando: el 1 es el jugador 1 y el 2 el jugador 2, con cruceta y stick izquierdo, gatillos como botones; probado con
mandos virtuales de SDL3 (`input_mapping`).

## D43 (2026-10-19): Web con Emscripten: llamadas en cola y excepciones de wasm, SDL3 de la ruta oficial de puertos, el archivo lo da el usuario

Se compila con `-mtail-call -fwasm-exceptions`: el código generado necesita `musttail` (llamada en cola de wasm) y
`setjmp`/`longjmp` (D39) por el mecanismo nativo de wasm, no el de JS. SDL3 viene del puerto oficial
(`--use-port=sdl3`, 3.4.2), sin copiarlo al repositorio. La página (`app/web/shell.html`) pide `sf2.zip` con un selector
de archivo o arrastrándolo, lo escribe en el sistema de archivos en memoria y llama a `main`; la configuración y los estados
van a IndexedDB (`/persist`, IDBFS). No hay ROMs en la página ni en el `.wasm`, pero el `.wasm` sí lleva el programa del 68000
traducido a C y compilado, es decir, deriva del código de Capcom igual que `generated/` (D5): el build web es para uso
personal y no se publica; la página lo dice. Probado en el navegador integrado de la aplicación (Chromium); Firefox y Safari
no probados (necesitan llamadas en cola y excepciones de wasm: Firefox 121+ y Safari 18.2+).

## D44 (2026-10-19): Linux por Docker con clang, Windows por llvm-mingw local; GCC anterior a 15 no se soporta

Linux: imagen de Debian 13 con clang 19, CMake, Ninja, Python y `libsdl3-dev` (`docker/linux.Dockerfile`,
`tools/linux-docker.sh`), con el repositorio y `sf2.zip` montados de solo lectura y el árbol de compilación en un volumen
de Docker: no se instala nada en la máquina anfitriona. Se prueba en arm64 (nativo en Apple silicon) y x86-64 (emulado).
Windows: `llvm-mingw` (clang + lld + UCRT para macOS) y las bibliotecas de SDL3 para mingw en `external/`, con el archivo de
toolchain `cmake/toolchains/mingw-x86_64.cmake`; los `.exe` solo necesitan `SDL3.dll`. Se prueban con Wine dentro de Docker
(`tools/windows-wine-docker.sh`), que es una prueba de humo y no sustituye a Windows. Los compiladores soportados son clang >= 13 y
GCC >= 15 (`runtime/sf2_port.h` falla al compilar si no hay `musttail`): GCC 14 de Debian 13 no vale y se usa su clang.
MSVC no está soportado (no tiene `musttail`).

## D45 (2026-10-19): Igualdad entre plataformas: estado y vídeo idénticos en todas; audio idéntico solo donde el compilador contrae a FMA como MAME

`tests/trace.c` (`sf2_trace`) imprime, cada 1.000 cuadros de la partida de validación, el CRC-32 del estado completo de la
máquina, del cuadro de vídeo y del audio acumulado. macOS arm64, Linux arm64, Linux x86-64, WebAssembly (Node) y Windows
x86-64 (Wine) dan el mismo estado y el mismo vídeo en los 12 puntos (y el mismo resumen de 12.000 cuadros que el arnés de los
hitos 2 y 3). El audio es bit a bit el de MAME en arm64 (macOS y Linux: mismo hash del WAV); en x86-64 y wasm, sin FMA, 985 de
9.742.081 muestras difieren como mucho 1 LSB (SNR 111 dB) y los siete tramos pasan los criterios de energía y espectro de D33
con diferencia 0,000 dB. Es lo que D31 anticipaba, ahora medido: la igualdad bit a bit del sonido depende de la arquitectura
y el criterio de D33 es el que vale en las demás.

## D46 (2026-10-19): La red de seguridad de Musashi es opcional y un salto a código no recompilado nunca es silencioso

Cumple lo que D19 dejaba para el hito 6. `sf2_blind` solo interpreta con Musashi direcciones sin función recompilada si se
pide con `--fallback`; por omisión se detiene como el juego. Todo despacho pasa por `rt_dispatch` (un único punto: busca en la
tabla, avisa al gancho de cobertura y, si no está, `rt_unknown`), y la máquina guarda origen y destino del último salto
fallido. El mensaje dice destino, instrucción de origen y remedio (`entry XXXXXX` en `tools/hints.txt`). El juego, además, lo
escribe en `errores.log` (carpeta del perfil) con la fecha y guarda el último estado bueno (como mucho 600 cuadros antes, una
copia en memoria) como `fallo_cuadroN.sta`, que se carga con `--load-state`; en una partida guiada (`--frames`) termina con
error. Motivo: con la red puesta, la cobertura de las secuencias no habría encontrado la tarea del final de los personajes
(0x94C9E), que en el juego habría parado la partida tras vencer a M. Bison. Se descartó dejar la red en el arnés por omisión
porque mide otra cosa que el ejecutable que se entrega.

## D47 (2026-10-19): El código generado lleva el reloj exacto del 68000 de MAME 0.289, no el de Musashi

Sustituye la última frase de D16 («no se persigue el reloj exacto»). Cuatro cambios, cada uno medido con trazas de MAME
(`totalcycles`) frente a `sf2_blind --trace-cycles` (bitácora `2026-10-19-hito6-reloj.md`): los costes de las pocas clases de
instrucción en que la tabla de Musashi difiere del 68000 (operaciones .l entre registros, `adda/suba.l` desde registro,
`andi.l #,Dn`, `addq.w #,An`, operaciones de bit con bit < 16, la cuenta de MULS); el reset de 34 ciclos; la entrada a la
interrupción sincronizada con el reloj E (primer ciclo ≥ fin + 50 que es 5 módulo 10); y el punto de muestreo de la IRQ, que
depende del microcódigo de cada instrucción (fin del último acceso al bus antes de su latch IR→IRD), tabulado por
`tools/mame_m68000_ack.py` en `recompiler/m68000_ack.txt` y aplicado en `rt_hook` con un código por instrucción (`c->ke`).
Motivo: con el reloj de Musashi 6 de las 14 secuencias largas divergían de MAME para siempre (una IRQ cortaba una tarea larga en
otro punto, la tarea acababa en otro cuadro y el estado no se recuperaba); con el reloj exacto la partida de validación es
idéntica a MAME en todos los campos de los 12.000 cuadros, también PC interrumpido, registros y pilas. `gen_c.py --clock
musashi` conserva el modelo anterior; el paso cerrado con Musashi sigue validando la semántica (entrega la IRQ con el reloj de
Musashi en los dos lados). La tabla son números derivados del microcódigo de MAME, no código suyo. DIVU/DIVS exactos no se
implementan (el juego no ejecuta su único DIVU).

## D48 (2026-10-19): Las secuencias que recorren el juego las genera un jugador con búsqueda sobre el núcleo recompilado

`tools/playgen.py` juega con la fachada del núcleo como biblioteca compartida (`libsf2py`, ctypes) y graba lo que pulsa, en el
formato de las secuencias con directivas `#@` (`tools/sequences/README.md`). En los combates prueba cada maniobra desde un
estado guardado y se queda con la que más daño hace y menos recibe; repite un asalto perdido con otra semilla. Fuera de los
combates sigue las pantallas por variables de la RAM encontradas para ello. Motivo: ganar los 11 combates con cada personaje a
ciegas es inviable a mano, y grabar en MAME con `-record` exigiría jugar de verdad; un jugador sobre el núcleo es rápido
(~10.000 cuadros por segundo sin vídeo) y lo grabado se reproduce igual en MAME porque las entradas se aplican en la entrega de
la IRQ2, como al leer una secuencia. Cada secuencia anota el CRC del estado final, que `playgen.py --verify` comprueba. La
validez frente a MAME no se supone: se comprueba con `make compare-coverage`.

## D49 (2026-10-19): Heurísticas del análisis más estrictas y cero conflictos como invariante

`tools/analyze.py`: el retorno por registro (`lea ret(pc),An ; bra sub`) exige que la lea y la transferencia estén en el mismo
tramo lineal y que la subrutina vuelva por `jmp (An)` con el mismo registro; los destinos de cualquier tabla, no solo los de
ramas directas, acotan las demás tablas; los argumentos de tarea se leen también en las llamadas en cola (`jmp`) a un
envoltorio de `trap`; y la nueva corrección manual `inline_text RUTINA` describe las rutinas que leen un texto en línea tras el
`bsr` (análisis y generador saben que vuelven detrás del texto). `Analysis.undecoded_successors()` lista todo destino de rama,
llamada o caída que no sea una instrucción decodificada; debe ser 0 (sale en el informe de `analyze.py` y en el de cobertura) y
lo que quede se resuelve con `table_end`/`entry`/`label`/`inline_text` en `tools/hints.txt`, con el motivo en cada línea (D12).
Motivo: los cuatro fallos que encontraron las partidas completas venían de estas heurísticas (datos decodificados como código
que tapaban instrucciones reales, una tabla que se comía código, una tarea creada con llamada en cola).

## D50 (2026-10-19): Los dipswitches de una secuencia valen desde el encendido también en MAME

La secuencia los declara (`#@ dips`) y el núcleo los fija antes del reset. En MAME, `tools/mame_blind.lua` (`SF2_DIPS`) los pone
con `user_value`, pero eso solo llega al puerto en la siguiente actualización de entradas, y el 68000 lee DSWC al arrancar (modo
de pruebas). Por eso `tools/coverage.py` hace antes una pasada de un cuadro con un `-cfg_directory` propio: MAME guarda los
dipswitches en ese cfg al salir y la pasada buena los carga al encender; el script comprueba en el cuadro 1 que el puerto lee
lo pedido. Nunca se toca `external/mame-data/cfg`. Se descartó escribir el XML del cfg a mano (depende de los valores por
omisión de cada campo).

## D51 (2026-10-20): Una optimización solo entra si todas las huellas salen idénticas a las del binario anterior

Optimizar el núcleo (hito 6) no puede cambiar nada observable, ni siquiera dentro de lo que MAME toleraría. Criterio: con el
binario anterior y el nuevo, el resumen por cuadro (con PC, SR y registros), los comandos de sonido, las direcciones ejecutadas
y el registro de despachos de las 14 secuencias tienen que ser idénticos byte a byte, `playgen.py --verify` tiene que dar los
mismos CRC de estado y uno de cada 37 cuadros renderizados tiene que dar el mismo PNG (16.291 capturas). Con eso entran: tablas
precalculadas en `gfx_init` (mapeador y paleta a RGB, 1,25 MB), información de tile por fila de tiles, bucles de píxeles
especializados por capa, recorte de sprites y un camino rápido en `rt_hook` (bitácora `2026-10-19-hito6-rendimiento.md`). Se
descartó saltar el bucle de espera del juego hasta la siguiente IRQ: daría bastante más velocidad al núcleo, pero exigiría
reproducir sin ejecutar el punto exacto de muestreo de D47 y el contador de instrucciones, y el juego ya va a 31 veces el tiempo
real con todo.

## D52 (2026-10-20): Sanitizadores como opción del build, con parada al primer aviso

`SF2_SANITIZE` (CMake) compila todo el proyecto con los sanitizadores pedidos y `-fno-sanitize-recover=all`, para que un aviso
sea un proceso que falla (lo ven `ctest`, `tools/coverage.py` y cualquier guion) y no una línea que se pierde en la salida. El
comportamiento indefinido que encontró en Musashi (desplazar `1 << 31` sobre `int`) se corrige en la copia local con un parche
anotado (`third_party/musashi/PARCHES.md`) en vez de excluir Musashi del sanitizador: el arnés de paso cerrado es una referencia
y tiene que estar limpia. En macOS arm64 no hay LeakSanitizer; las fugas se miden con `leaks --atExit` sobre una copia del juego
firmada con `get-task-allow` (sin ese derecho `leaks` no ve el montón y su «0 fugas» no vale).

## D53 (2026-10-20): Pantalla ancha real como ancho de cuadro configurable, con escenas que no se ensanchan centradas

El mod de pantalla ancha (plan en `docs/WIDESCREEN.md`) no estira: el renderizador dibuja más columnas a cada lado. El ancho del
cuadro entregado (`Video.w`, `sf2_set_video_width`) va de 384 (apagado, idéntico a MAME) a 512 (techo: la X de los sprites es de 9
bits; con la forma de píxel de la recreativa 512 da 16:9). Decisiones: (1) el ancho del cuadro es constante: las escenas
compuestas para 384 (título, mapa, selección, VS, diálogo, bonus, final) se dibujan en 384 y se centran con barras negras, y solo el
combate con oponente (`video_scene_wide`, por la RAM del juego) usa todo el ancho; así la textura y la ventana no cambian con la
escena. (2) El ancho no es estado de la partida: no entra en los estados guardados ni cambia la ejecución. (3) Se prueba con
una regla dura: en toda escena las 384 columnas centrales del cuadro ancho son idénticas al cuadro de 384 (`roms_and_states`), y con
el modo apagado `compare-video` sigue dando 17/17. (4) Esta primera entrega no toca la lógica del juego; el relleno del arte
en los extremos de la cámara queda para la fase 2.

## D54 (2026-10-20): Relleno del arte en los extremos de cámara por reflejo, solo fuera de la ventana de la recreativa

El juego carga el arte completo de cada escenario en los tilemaps de SCROLL2 y SCROLL3 (anillo de 64 columnas) al empezar y no lo
cambia al mover la cámara, así que el borde del arte se lee de la propia RAM gráfica (medido en Honda: 53 columnas de SCROLL2 y 21 de
SCROLL3 con tiles visibles, el resto vacío). Con la vista ancha, en un extremo, la cámara asoma por ese hueco. Decisiones: (1) se busca
el tramo circular más largo de columnas sin tiles visibles y se ensancha por filas con las columnas contiguas vacías en esa fila (el
suelo acaba antes que el techo; un tile es «visible» si algún pen no vale 15, tabla `Gfx.blank` calculada en `gfx_init`); cada
columna del tramo toma, reflejada, la columna de arte más cercana al borde. (2) Solo se rellena fuera de la ventana de la recreativa
(coordenadas crudas 64-447): dentro es el hueco original, así que el centro de 384 columnas sigue idéntico al cuadro de 384 por
construcción, y se comprueba con 42.000 cuadros de Ryu y de Zangief (0 líneas distintas). (3) Se descartó mover las capas para que
no asomen (cambiaría el centro y desincronizaría los sprites) y repetir la última columna (deja rayas de 64 px). (4) SCROLL1 (el
marcador) no se toca. Límite conocido: el reflejo es una invención plausible, no arte de Capcom; los fondos con rótulos o figuras junto
al borde muestran su copia reflejada en la banda de 64 px. Con anillo lleno (sin columnas vacías) no se rellena nada.

## D55 (2026-10-20): HUD en los bordes por ranuras de la tabla de objetos, como opción apagada por omisión

Con la vista ancha el marcador del combate queda en el centro de 384 píxeles, con las barras lejos de los bordes. El marcador entero son
sprites, no SCROLL1 (SCROLL1 solo tiene una banda gris de fondo), y ocupa ranuras fijas de la tabla de objetos que el juego rellena
igual en la presentación, la pelea, la victoria y el recuento: 0-13 puntuación del 1P y del 2P o INSERT COIN, 14-25 tiempo y récord
(centro), 26-36 nombre y marca de rondas del 1P, 37-43 nombre del 2P, 44-63 barras de vida, 72-75 KO y marcadores de vida (código
0x8302), 64-71 mensajes centrales (ROUND, recuento) y de la 76 en adelante luchadores y efectos. Decisiones: (1) la opción `hud = edges`
(`--hud-edges`, `sf2_set_hud_edges`) mueve cada entrada de esas ranuras ±64 px según caiga a un lado u otro del centro de la placa (256);
tiempo, récord, KO y mensajes se quedan; los luchadores nunca se tocan. (2) Se decide por ranura y no por posición porque los luchadores
saltan por la banda superior y porque filas como la del récord no son simétricas. (3) Se descartó estirar las barras: las dos mitades
quedan con un hueco de 64 px junto al KO, como en otros parches de pantalla ancha. (4) Invariante probado: con la opción activa el cuadro
no cambia fuera de las 48 primeras filas (`test_widescreen`, 42.000 cuadros de Ryu y de la secuencia de dos jugadores). Límite: depende
de la asignación de ranuras del juego; si una pantalla usara otras ranuras para el marcador, esa parte quedaría en el centro.

## D56 (2026-10-20): Criterio de escena ancha también para la demostración, y el marcador solo en la banda superior

Prueba de uso real (la demostración del modo atracción salía con barras negras): el criterio de D53 solo miraba el modo 2 (partida). En la
demostración el modo (`0xFF8001`) cambia con la pantalla: 0x0A con fase 2 es la selección (subfase 0) y el combate (subfase 2), y 0x0C con
fase 2 es la victoria; ahora cuentan como combate. Además el «INSERT COIN» del demo usa las ranuras 0-10 de la tabla de objetos a la
altura y=184, así que `hud = edges` lo partía en dos mitades: `hud_side` ignora los sprites con y >= 80 (el marcador siempre está arriba).
Sobre los elefantes de Dhalsim y el barco de Ken: no hay arte más allá (el tilemap solo tiene esas columnas y se carga entero al
empezar); se probó extender el borde por fila con otro umbral y no cambió nada, así que se deja el reflejo actual.

## D57 (2026-10-20): Pantallas fijas con barras del color de fondo; bonus ensanchados

Las pantallas compuestas para 384 (selección, mapa, VS, final, créditos) no se ensanchan renderizando más tilemap: lo que sobra a los lados
es el envoltorio del anillo de 64 columnas (retratos y «VS» duplicados) o arte que no existe, no arte que falte. Se dibujan solo en la ventana
original y las barras laterales toman el color del fondo de la pantalla, el del píxel del borde en la fila de arriba (azul en el mapa, negro en el VS y el final).
Se probaron y descartaron: continuar el color del borde fila a fila (rayas en la selección y el final) y exigir tramos planos. Los bonus
(mismo modo que el combate, sin oponente) se ensanchan como un combate: el relleno por reflejo de D54 los cubre y se quitó la condición del
oponente (`0xFF86C6`) de D53.

## D58 (2026-10-20): Lógica ancha como parches en el recompilador, con un campo en la CPU que los apaga

La lógica del juego atada a los 384 píxeles se cambia en el código recompilado, no escribiendo en la RAM cada cuadro: `recompiler/mod_patches.txt`
lista, por dirección, un fragmento del C que emite `gen_c.py` y su sustituto; el generador exige exactamente una aparición y falla si
una dirección no se emite. Todos los parches leen `c->mod_wide` (píxeles extra por lado: 0 = original) y valen lo mismo que el original
con 0, sin tocar ciclos ni el reloj de MAME. Puntos tocados (siete instrucciones): `0x2D98` (pared de un objeto contra la cámara: margen
izquierdo y ancho 0x180) y `0x83650` (seguimiento de la cámara: ancho 0x180 y topes 0x9D0/0x9D2 del escenario). Así el luchador llega al
borde de la ventana ancha y la cámara sigue encuadrando dentro del arte; los proyectiles y objetos usan las mismas paredes. Interruptor:
`sf2_set_wide_logic` / `wide_logic` / `--wide-logic` / tecla F3, efectivo solo con el cuadro ancho; el reinicio de la CPU conserva
`mod_wide` y no es parte del estado guardado. Comprobaciones: el C generado difiere del anterior solo en esas siete líneas; con el
interruptor apagado las 14 secuencias dan los mismos CRC de estado que la grabación (`playgen.py --verify`) y `make test` y `compare-video`
pasan; encendido, un luchador llega a 64 px más lejos de cada pared (separación máxima 291 → 419 en la prueba de caminar) y la partida
deja de coincidir con MAME. Límites: no se han revisado a fondo los movimientos que usan los topes de otra forma (ataques que empujan
al rival contra la pared, proyectiles que se descartan al salir de pantalla, jefes de bonus, IA) y la dificultad de la IA no está reajustada.

## D59 (2026-10-20): Stage completo con cámara fija (copia SF2Recomp-ultrawide)

Para monitores ultraanchos: el combate se ve entero, sin scroll, y los luchadores recorren todo el escenario. Medido: las 12 pantallas de lucha
comparten el recorrido de la cámara (topes `0x9D0 = 0x100`, `0x9D2 = 0x280`, tabla de `0x83D3A`), así que el mundo mide 768 px (X de mundo 256-1024);
el cuadro de 768 lo muestra entero y el de 688 (3440x1440 con `aspect = 4:3`) deja 40 px sin ver por lado. Decisiones: (1) cámara fija: con
`c->mod_fixcam` el seguimiento de cámara (`0x8365E`) fija la cámara principal en el centro de sus topes y salta al RTS; las cámaras de las capas
derivan de ella, así que todas quedan fijas (el parallax se queda estático, el rowscroll del suelo conserva su perspectiva). (2) Las paredes de
los luchadores (D58) y las ventanas «¿está a la vista?» de objetos, escenario y proyectiles (0x2816, 0x28D6, 0x290E, 0x2944, 0x47DC, 0x4804, 0x482C,
0x2D46E, 0x33218, 0x33296) se ensanchan con `c->mod_wide` píxeles por lado; el cuadro mide 384 + 2 * mod_wide. (3) El constructor de sprites (8
copias en `0x7F256`...`0x7F844`) descartaba los fragmentos con X sin signo >= 512; ahora admite X en [-mod_wide, 512 + mod_wide) y la guarda con
signo en la tabla de objetos, sin envolver en 9 bits (el juego ya enmascaraba solo la Y); el renderizador la lee con signo (`Video.xsigned`).
(4) `mod_wide` y `mod_fixcam` valen solo mientras la escena dibujada es de las que se ensanchan (se fijan en cada VBlank): en los menús el juego
corre sin parches, porque una X negativa se vería enmascarada a 9 bits en un cuadro de 384. (5) Interruptor `stage_full` / `--stage-full` / F4,
apagado por omisión; apagado, el código parcheado es el original (14 secuencias con el mismo CRC de estado que la grabación; `compare-video` 17/17).
No hay estado guardado propio: se puede alternar en caliente. Límites: no se han revisado la IA, los jefes ni cada ataque que usa la pared;
los objetos de escenario que el juego solo coloca cerca de la cámara original se ven donde el juego los pone; los bonus conservan sus puntos
de aparición.

## D60 (2026-10-21): Mod de entrenamiento como gancho de VBlank en el núcleo, con los visores en el anfitrión

El modo entrenamiento (`docs/ENTRENAMIENTO.md`) necesita tocar la RAM y las entradas del juego y dibujar datos propios. Decisiones: (1) el gancho está en el núcleo
(`runtime/training.c`, llamado desde `on_vblank` de `sf2.c`, que ahora se instala siempre) y actúa justo antes de entregar la IRQ2, el punto donde el juego lee las entradas;
así es determinista y portable (Web incluida) y no depende del anfitrión. (2) Las entradas del anfitrión se guardan tal cual (`host_in`) y el mod calcula las que ve el
juego: con `lock_p2` descarta las de J2 y mete las del dummy; el versus automático suma monedas y START1+START2 en el título. (3) El núcleo no dibuja: entrega el historial de entradas
y la lista de cajas (`sf2_training_history`, `sf2_training_boxes`) y `app/main.c` los pinta con la resolución de la ventana, sin tocar la imagen del juego ni el estado. (4) Interruptor
doble: `enabled` (cambia el juego) y `track_inputs` (solo registra); con ambos a 0 no se toca nada y la partida es idéntica a MAME, comprobado con el CRC de estado
(`test_training`) y con `compare-video` 17/17. (5) Direcciones: temporizador `0xFF8ACE` (BCD) y su subcontador `0xFF8ACF` (30 → 1; fijar solo el temporizador deja el marcador en 98),
vida `jugador+0x30` (máximo 0x90); el formato de las cajas lo documenta `docs/HITBOXES.md`. (6) El dummy `block` se basa en los botones que pulsa J1 y no en que su ataque esté activo. No es
parte del estado guardado.
