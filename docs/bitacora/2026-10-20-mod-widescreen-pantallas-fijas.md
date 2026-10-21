# 2026-10-20 · Mod widescreen · Pantallas fijas y bonus

## Qué se hizo

Se forzó el ancho 512 en todas las pantallas para ver qué pasa (copia de `video.c` en el scratchpad) y se miró capa por capa la selección, el VS, el título y el final.
Hallazgo: los duplicados de los retratos y del «VS» son el envoltorio de SCROLL2 (anillo de 1024 px) y no se pueden rellenar con arte. Título, advertencia y mapa
(SCROLL3 azul) ya se veían bien a 512. Implementado en `runtime/video.c`: las escenas de 384 se dibujan en la ventana y las barras toman el color de fondo (D57);
los bonus se ensanchan como un combate (se quitó la condición `0xFF86C6`).

## Resultados

- Selección, mapa, VS, título, final y créditos: sin duplicados; barras azules en el mapa y la selección, negras en el resto.
- Bonus de coches y barriles: ensanchados con el relleno por reflejo.
- Centro de 384 columnas idéntico y HUD en bordes sin tocar nada bajo el marcador: `validacion.txt` 12.000 cuadros, `ryu.txt` y `versus.txt` 42.000; `make test` 8 de 8; `compare-video` 17 de 17.
- Límites: junto al retrato de la selección la barra azul termina en el borde del retrato; el panel del final queda con barras negras (es una imagen enmarcada).
