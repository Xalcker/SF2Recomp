# 2026-11-23 · Pantalla ancha, lógica ancha y stage completo en Street Fighter Zero (D76)

## Pregunta de partida

En sfzch estaban el estéreo, el entrenamiento y las cajas (D73-D75); faltaban la pantalla ancha, la lógica ancha y el stage completo
(D66, D71). Con las ROMs en `sfzch/` se pudo compilar, comparar con MAME y mirar capturas.

## Qué se midió

- **Estados** (`sf2py` con `tools/inputs_sf2.txt`): combate de la partida = modo 2, fase 4, subfase 4 (0xFF800D = 2 pelea, 4 KO, 6 victoria);
  los combates de la demostración son modo 0x10 con fases 2 y 6. El byte +0x4 de cada luchador (0xFF8404, 0xFF8804) vale 2 mientras hay
  luchadores, también en la demostración, salvo ~3 cuadros entre asaltos.
- **Cámara**: la mueve 0x1936C, objeto en 0xFF8280 (X 0x10, objetivo 0x20, topes 0x44 = 0x100 y 0x46 = 0x280 en todos los escenarios). La
  X es el borde izquierdo de la vista (448 = luchadores centrados); el recorrido del escenario es 256-1024 (768 px).
- **Paredes**: 0x1DBA0 (izquierda: x - semiancho - cámara <= 0; derecha: x + semiancho - cámara >= 0x180).
- **Marcador**: SCROLL1, no sprites (la tabla de objetos solo lleva luchadores y efectos).

## Problemas y cómo se resolvieron

1. Constructor de sprites que aparca (ver D76). Con la vista ensanchada los tiles aparcados en la X 448 se veían.
2. Con la X con signo siempre encendida, la intro de la demostración perdía piezas de bloques anclados fuera de la ventana: la X con
   signo pasó a depender de la escena (`xsigned_scene`).
3. Con 768 px el marcador (SCROLL1, anillo de 512) se repetía en los dos extremos: recorte a la ventana original.

## Verificación

- Apagados los mods: `make compare-mame` 12.000 cuadros con la misma RAM, RAM gráfica y entradas que MAME; `make compare-video` 17/17.
- `ctest` 8/8 en sfzch, WW y CE (`test_widescreen`, `test_wide_logic` y `test_stage_full` corren en sfzch; la prueba del HUD en los bordes no).
- Centro del cuadro de 512 comparado con el de 384 en 3.000 cuadros: 11 cuadros distintos (0,4 %), los mismos que en CE y HF: la tabla de la
  máquina sombra llega un cuadro desfasada (la prueba tolera hasta un 1 %).
- Capturas: 512 con lógica ancha (los luchadores llegan al borde de la vista ancha) y 768 con stage completo (cámara fija en 448, los
  luchadores recorren el escenario entero).

## Límites y pendiente

- **HUD en los bordes**: no existe en sfzch (el marcador es SCROLL1: habría que partir la capa por mitades).
- **Bordes del arte**: el relleno de los extremos (D54) se diseñó para el anillo de SF2; en sfzch quedan artefactos en el borde de algunos
  escenarios (una franja negra de ~32 px en el borde izquierdo del stage completo de Chun-Li, rectángulos claros en la esquina superior).
  No se resolvió.
- **No revisado**: la dificultad de la IA, los jefes, las paredes en los modos de bonus y otros usos de la cámara (0xFF8290) en movimientos
  especiales (0x24B94, 0x2A1E0, 0x2BAB0...: aparecer a un lado de la pantalla); con la vista ancha usan los 384 px originales.
- Medidas hechas solo con Ryu contra Birdie, Chun-Li, Adon y las demostraciones de `tools/inputs_sf2.txt`.
