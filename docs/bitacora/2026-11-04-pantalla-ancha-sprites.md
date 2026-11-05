# 2026-11-04 · Sprites en las bandas de la pantalla ancha (D62)

## Pregunta de partida

El usuario, tras jugar: «hay sprites que sobran o se dibujan en lugares incorrectos (la lámpara de Honda, las piedras de Chun-Li, los barriles de Ken), algunos
sprites se pasan del lado derecho al izquierdo (poderes lanzados), las bicicletas de Chun-Li aparecen de la nada», en todos los modos de pantalla ancha.

## Medidas

- Herramientas en el scratchpad sobre la fachada (no forman parte del repositorio): escáner de la tabla de sprites por cuadro (apariciones repentinas lejos del borde y
  bloques que pasan de la X 511), captura de tiras de cuadros en cada modo, comparación en paralelo de dos máquinas (384 contra ancho) y búsqueda de huecos de escena.
- 512 con la lógica original: la lámpara de Honda aparecía de golpe en x = 463 (cuadro 9081); 141 apariciones en Honda, 176 en Chun-Li, 99 en Ken.
- Lógica ancha a 512: el constructor (parcheado con `mod_wide` = 64) dejaba X hasta 575 y el renderizador no estaba en modo con signo: 486 bloques envueltos en Zangief.
- Ensanchar las ventanas en la partida real la cambia: la X del rival diverge en el cuadro 30.728 de `ryu.txt` (más ciclos de dibujo).
- La tabla latcheada en el VBlank N la construyó el cuadro anterior: la primera versión de la sombra (un cuadro) daba tablas idénticas a las reales.
- Huecos de escena ancha: subfase 0x0E (un cuadro, recuento) y modo 0x0C fase 0 en la demostración (un cuadro); la subfase 0x06 es pantalla negra.

## Qué se hizo

`mod_view`/`mod_bx` en la CPU y en `mod_patches.txt`; máquina sombra en `runtime/sf2.c` (`shadow_frame`, `machine_state_ex` sin sonido); recorte de bloques en
`render_sprites`; criterio de escena en `video_scene_wide`. Arreglado de paso: la sombra compartía el guion de entradas y `machine_free` lo liberaba dos veces.

## Resultado

- Apariciones repentinas en 512: Honda 141 → 59, Ken 99 → 42, Chun-Li 176 → 107 (las que quedan son objetos que nacen en ese momento: chispas, inicio de asalto,
  piezas de los bonus). Visto en tiras: la lámpara de Honda y las bicicletas entran desde el borde; los barriles y sus trozos no dan la vuelta.
- Partida real idéntica a la original con pantalla ancha: 0 direcciones de RAM distintas en `ryu.txt` (512), `ken.txt` (512) y `chunli.txt` (640).
- `make test` 8/8, `make compare-video` 17/17. Rendimiento con vídeo: 3.306 → 1.807 cuadros por segundo con pantalla ancha.
- En el stage completo los saltos de ±512 que señalaba el escáner son pares reales (dos cajas en los extremos del mundo de Guile, las dos sombras de los luchadores).

## Pendiente

Que el usuario lo pruebe jugando en los tres modos. La versión Web no se ha medido con la sombra.
