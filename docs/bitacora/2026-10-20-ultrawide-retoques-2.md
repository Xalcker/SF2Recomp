# 2026-10-20 · Ultrawide y HFR · Segunda tanda de retoques

- SCROLL3: el reflejo del arte se limita a 2 tiles (`fill_row_cols`, `d > 2`). Quita el tramo de árbol y raíces del stage de Blanka (se veía un bloque con borde vertical a la izquierda).
- Parpadeo de tiles en el stage de Ryu: no se pudo reproducir con certeza. Se descartó que lo cause el relleno (`art_run_find` da siempre el mismo tramo en los 654 cuadros de combate medidos) y el rowscroll (sin cambios en esos cuadros); el mapa de píxeles que alternan A-B-A mezcla movimiento real del suelo con parpadeo. Falta saber en qué modo (F1 encendido o apagado, 512 o ultraancho) y en qué zona.
- `make test` 8 de 8 y `make compare-video` 17 de 17.

## Avión de Guile en widescreen
- Con la cámara a la derecha, el borde del arte de SCROLL2 caía sobre el ala del avión y el reflejo dibujaba aletas, misiles y X sueltas (x 448-512 a 512 px).
- Cambio (`tile_solid` en `runtime/video.c`): en SCROLL2 no se refleja un tile con píxeles transparentes (silueta de un objeto cortada por el borde); queda vacío y se ve lo que hay detrás (el cielo). Los tiles sólidos (suelo, cielo, muros) se siguen reflejando.
- Comprobado: Guile a 512 y 800 px con la cámara a la derecha, y Honda con la cámara en los dos extremos (sin huecos nuevos). `make test` 8 de 8, `compare-video` 17 de 17, `--bench-mode video` 2.517 cps (antes ~2.550).

## Revisión de los 12 stages y arreglos (widescreen 512 y ultraancho 768)
Método: guiones de `tools/sequences/ryu.txt` hasta cada combate y andar hacia cada pared; capturas en los dos extremos; mapas por capa (máscara de capas) y de relleno (`SF2_FM`, ya retirados).

| Stage | Fallo | Arreglo |
|---|---|---|
| Ryu (izq.) | hueco negro bajo el tejado | SCROLL2: filas de poco arte cuyo corte queda a ≤ 8 tiles del borde del escenario se extienden y se reflejan (`FILL_NEAR`) |
| Honda | rendija negra entre los estandartes | `fill_narrow_holes`: huecos de ≤ 24 px sin dibujar fuera de la ventana original se rellenan con el píxel vecino de la fila |
| Sagat (izq.) | bloques sueltos del Buda | el mismo relleno de filas con corte cerca del borde |
| Dhalsim | elefantes cortados con borde vertical | sin SCROLL1/SCROLL3 (elefantes de primer plano) también en widescreen, no solo en ultraancho |
| Guile y Ken | trozos de ala, misiles y cascos | SCROLL2: no se refleja un borde con siluetas (tiles transparentes) en los 3 tiles vecinos (`silhouette_near`); solo en esos dos escenarios |
| Blanka | tramo de árbol suelto | (arreglado antes) SCROLL3 refleja como mucho 64 px |

Escenarios por personaje del oponente (RAM 0xFF8668, `STAGE_*` en `video.h`); las reglas por escenario son datos del mod.

Queda como arte original, no se rellena: la zona oscura a la izquierda de Zangief (tiles sólidos del propio arte) y las barras negras de Balrog (tiles negros sólidos, no huecos). En Ryu queda una esquina negra mínima abajo a la izquierda y en Ken algún trozo de casco de barco a la derecha.
Comprobado: `make test` 8 de 8, `compare-video` 17 de 17.

## Zangief y Balrog (a petición)
- **Zangief:** la pared oscura a la izquierda era un relleno de tiles casi negros (todos los píxeles opacos con canales ≤ 48) en el anillo de SCROLL2/3. `art_blank` (`runtime/video.c`) cuenta esos tiles como vacíos en Zangief al buscar dónde acaba el arte, así que el reflejo existente los sustituye por el andamio. Memoria `tile_dark` por (código, color) y por cuadro.
- **Balrog:** primero se reflejó la ventana de la recreativa sobre los márgenes (`mirror_margins`), pero eso es un reflejo de pantalla y se movía con el escenario; se retiró. Lo que vale es el reflejo anclado al arte: con el id de jefe correcto (10) la regla de tiles oscuros (`art_blank`) ya aplica a Balrog y el relleno de siempre refleja el borde real del arte, que se mueve con la cámara. El texto de los letreros sale invertido.
- **Corrección de ids:** los jefes son 8 Bison, 9 Sagat, 10 Balrog, 11 Vega (el orden supuesto antes era otro). Las reglas de Guile (3), Ken (4), Zangief (6) y Dhalsim (7) no cambian.
- `make test` 8 de 8, `compare-video` 17 de 17, `--bench-mode video` 2.580 cps. Capturas en `docs/capturas-stages/` (`balrog_*_despues.png`).
