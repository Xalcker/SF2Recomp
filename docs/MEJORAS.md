# Ideas de mejora y mods para el port

Fecha: 2026-10-20. Estado: propuestas, nada implementado todavía.

Estas ideas aprovechan que el juego ya corre como C nativo sobre una capa CPS1
propia: tenemos acceso directo a la RAM del juego, a las entradas y al
renderizador, sin intérprete de por medio. Las cinco comparten una base común:
una **capa de mods** (`runtime/mods/`) que se ejecuta una vez por fotograma,
lee y escribe la RAM útil y dibuja una superposición (overlay) encima del
vídeo CPS1. Conviene construirla primero porque las demás ideas se apoyan en ella.

Regla heredada del proyecto: los mods se activan solo en el ejecutable y nunca
modifican ni distribuyen datos de Capcom. Todo mod debe poder apagarse y dejar
el juego idéntico al original (se verifica con las 14 secuencias de MAME).

---

## 0. Base común: capa de mods y overlay

- Un punto de enganche al final de cada fotograma lógico (tras la VBlank del 68000).
- Un renderizador de overlay en resolución nativa de la ventana (no 384x224), para
  que el texto salga nítido al escalar.
- Un archivo de configuración (`mods.toml`) con cada mod y sus parámetros.
- Mapa de direcciones de RAM documentado en un solo sitio (salud, temporizador,
  estado del personaje, contadores de daño). Las direcciones se obtienen con los
  scripts Lua de MAME que ya usamos y se registran en `docs/` antes de usarlas.

**Hecho cuando:** un mod de prueba dibuja un texto y lee un valor de RAM sin
alterar el resultado de las secuencias de referencia.

---

## 1. Modo entrenamiento

Lo que pediste, más algunos extras habituales en juegos de lucha:

- **Sin límite de tiempo:** fijar el temporizador de ronda en 99 cada fotograma.
- **Barras que se recuperan:** si pasan N fotogramas sin recibir daño (por
  ejemplo 60-120, configurable), la barra de energía vuelve a llena, ya sea de
  golpe o a ritmo constante. Hay que detectar el "daño recibido" comparando la
  salud entre fotogramas, no mirando la animación.
- **Reinicio de posiciones** con un atajo (ambos jugadores al centro o a una
  esquina) y reinicio de la ronda sin pasar por la pantalla de victoria.
- **Dummy configurable** (ver abajo en el menú): quieto, agachado, saltando,
  bloqueando todo, bloqueando tras el primer golpe, o repitiendo una grabación.
- **Grabación y reproducción de entradas** del jugador 2 (el dummy repite una
  secuencia). Encaja con nuestro registro de entradas ya usado para las pruebas.
- **Medidor de daño y combo:** daño del último golpe, daño total del combo,
  número de golpes. Son contadores que se pueden derivar de la salud.
- **Barra de super/stun visible** si el juego guarda ese estado en RAM (a confirmar).

**Riesgos:** escribir salud o temporizador en RAM puede chocar con lógica del
juego que compara valores (por ejemplo, detección de KO). Probar que el KO sigue
funcionando cuando el modo está desactivado y que no se dispara de forma
espuria cuando está activado.

**Hecho cuando:** una partida de 5 minutos contra el dummy no termina por
tiempo, la barra se rellena tras el retardo configurado y el modo apagado
reproduce las secuencias de MAME sin diferencias.

---

## 2. Visor de entradas en pantalla

- Historial de las últimas N entradas por jugador (flechas en notación numérica
  o iconos, botones, y fotogramas que se mantuvo cada una), como en los modos
  de entrenamiento de los juegos modernos.
- Indicador de cada botón en tiempo real (joystick y los 6 botones) para vídeo
  y análisis de errores de ejecución.
- Contador de **ventanas de movimiento especial**: mostrar cuántos fotogramas
  tardó en completarse un cuarto de círculo o un dragon punch, útil para
  aprender el motor del juego.
- Posición configurable, tamaño, opacidad y modo "solo cuando hay entrada".

**Detalle técnico:** leer las entradas en el punto donde el juego las lee
(registro de entrada de la capa CPS1), no en el teclado o mando del sistema
operativo, para que el visor coincida exactamente con lo que el juego recibe,
incluida la lógica de SOCD si la hay.

**Hecho cuando:** el historial de una secuencia grabada coincide, fotograma a
fotograma, con el archivo de entradas usado en las pruebas.

---

## 3. Menú integrado para DIP switches y opciones

Un menú dentro del propio juego (abrir con una combinación, por ejemplo
Coin + Start o una tecla del teclado) para:

- Cambiar los **DIP switches** de la placa: dificultad, número de vidas o
  rounds por partida, daño del juego, continuaciones, coins por crédito,
  flip de pantalla y modo de prueba. Aplicarlos sin reiniciar cuando el juego
  los relea; en caso contrario, reiniciar la placa de forma automática.
- Opciones del port: escalado y filtros, modo del overlay, modos de FPS (idea 4),
  volumen, mapeo de mandos, activar y desactivar cada mod.
- **Perfiles guardados** (arcade fiel, entrenamiento, torneo) que cambien varios
  ajustes de una vez.
- Acceso al **menú de servicio** original del juego como entrada del menú.

**Detalle técnico:** los DIP viven como puertos de entrada de la capa CPS1
(`0x800018` y vecinos según la placa CPS-B-11). Guardar el estado en un archivo
de configuración y validar que cada combinación sea válida para esa revisión
(World 910522), porque algunas combinaciones no existen en el hardware real.

**Hecho cuando:** cambiar la dificultad desde el menú altera el comportamiento
del juego como lo hace en MAME con el mismo DIP, y el menú se maneja con
teclado y mando.

---

## 4. Más FPS: 100, 120, 144, 240 Hz y más

El juego corre a ~59,6 Hz y toda su lógica está atada a ese reloj. Cambiar la
velocidad de la lógica alteraría el juego (la física, los frames de animación,
el sistema de golpes y el aleatorio son por fotograma), así que la meta es
**fluidez visual con lógica intacta**:

- **Interpolación de renderizado:** mantener el 68000 a 59,6 Hz y renderizar a la
  tasa del monitor, interpolando posiciones de sprites (OBJ) y desplazamientos
  de planos de fondo (scroll) entre dos estados lógicos consecutivos.
- Desafíos conocidos en CPS1:
  - Los sprites se reciclan: un mismo índice de la tabla de objetos no siempre
    es el mismo personaje de un fotograma al siguiente. Hay que emparejar
    sprites entre fotogramas (por posición, tile y atributos) y no interpolar
    cuando hay un cambio brusco, como un teletransporte, un cambio de pose o un
    corte de escena.
  - Efectos de scroll por línea (parallax) que cambian a mitad de pantalla: la
    interpolación debe respetar los efectos por línea de raster que ya
    reproducimos.
  - Parpadeos intencionales (daño, invulnerabilidad): no interpolarlos.
- **Cuadro de mando:** modos "fiel (59,6 Hz)", "interpolado", y un modo con
  **sincronización variable (VRR)** donde la lógica sigue a su ritmo pero se
  presenta en cuanto está listo.
- **Latencia de entrada:** muestrear el mando lo más tarde posible antes de
  cada fotograma lógico; opcionalmente un modo "run-ahead" (reejecutar 1
  fotograma) para quitar uno de latencia, posible porque la CPU recompilada es
  muy barata y el estado completo cabe en pocos cientos de KB.
- 240 Hz y más: coste de CPU bajo en nativo, pero la interpolación en la
  web (wasm) necesitará medirse.

**Riesgos:** artefactos visibles (fantasmas, saltos) si el emparejamiento falla.
Siempre ofrecer el modo fiel y un conmutador para desactivarlo por escena.

**Hecho cuando:** a 120 Hz el movimiento se ve continuo en un combate completo
sin artefactos evidentes, y con la interpolación desactivada el vídeo es
idéntico al de MAME.

---

## 5. Herramientas de análisis: hitboxes, frame data y repeticiones

Para un juego tan estudiado como SF2, es el mod con más valor para la comunidad:

- **Visor de hitboxes y hurtboxes** (cajas de golpe, de daño, de agarre y de
  proyectil), con colores por tipo. Se calcula a partir de la RAM de cada
  personaje y de las tablas de cajas en ROM.
- **Frame data en vivo:** arranque (startup), activo, recuperación y ventaja
  tras golpe o bloqueo, calculada de forma automática en cada ataque.
- **Repeticiones deterministas:** como el juego es determinista (14 secuencias
  idénticas a MAME), basta guardar la semilla inicial y las entradas por
  fotograma. Archivos diminutos, reproducibles en cualquier plataforma del port,
  con avance fotograma a fotograma y rebobinado (con instantáneas periódicas
  del estado).
- **Guardado y carga de estados** (save states) para practicar situaciones.
- **Potencial a futuro:** juego en línea con rollback, apoyándose en lo
  anterior (estado pequeño, determinismo, avance rápido de la lógica). Es la
  meta más grande del lote y se evaluaría solo después de las demás.

**Hecho cuando:** las hitboxes de un ataque conocido coinciden con las de un
visor de referencia y una repetición grabada se reproduce con el mismo
resultado en macOS, Linux, Windows y web.

---

## Orden sugerido

| Orden | Idea | Por qué ahí |
|---|---|---|
| 1 | Base de mods y overlay (0) | Lo necesitan todas las demás |
| 2 | Visor de entradas (2) | Barato, útil para validar pruebas y el resto |
| 3 | Menú y DIP switches (3) | Hace configurables los demás mods |
| 4 | Modo entrenamiento (1) | Usa lo anterior; da el mayor valor al jugador |
| 5 | Más FPS (4) | Independiente y de más riesgo visual; se puede hacer en paralelo |
| 6 | Hitboxes, repeticiones, estados (5) | Largo plazo; abre la puerta al juego en línea |

## Ideas menores para anotar

- Filtros de pantalla: scanlines, CRT, píxel perfecto con escalado entero.
- Pantalla ancha: relleno del fondo en 16:9 en lugar de barras negras.
- Selección de personajes jefes en la pantalla de selección (si se confirma en RAM).
- Estadísticas de partida (golpes dados, bloqueados, precisión de combos).
- Modo "torneo": sin créditos, sin pantalla de continuar, rondas fijadas.
- Compatibilidad con mandos, mapeo por perfil y vibración en golpes.
