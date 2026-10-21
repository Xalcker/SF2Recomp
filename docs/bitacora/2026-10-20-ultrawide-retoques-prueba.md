# 2026-10-20 · Ultrawide · Retoques tras la prueba de juego

Solo en `SF2Recomp-ultrawide` (`runtime/video.c`, `runtime/video.h`). `make test` 8 de 8.

| Fallo visto | Causa | Cambio |
|---|---|---|
| Barras de vida lejos del centro con `hud = edges` | cada barra son 10 sprites (ranuras 44-53 y 54-63) y solo se desplazaban | `draw_hud_bar`: el remate pasa tal cual y el cuerpo (144 px) se estira para llegar hasta el KO |
| Chun-Li y Blanka: tiles que faltan o no encajan a la izquierda | el suelo ocupa las 64 columnas del anillo y `art_run_find` decidía que no había hueco que rellenar | se ignoran las filas con ≥ 60 columnas de arte y solo se rellenan tiles vacíos |
| Ken: bloque rosa suelto arriba a la derecha | SCROLL3 reflejaba la cubierta del barco, cuyo arte acaba antes que el del casco | en SCROLL3 no se refleja una fila cuyo borde queda ≥ 2 columnas dentro del borde del escenario |
| Dhalsim: elefantes de primer plano cortados | cuerpo en SCROLL3 y cabeza/trompa en SCROLL1 | contra Dhalsim (0xFF8668 = 7) con stage completo no se dibujan SCROLL1 ni SCROLL3 |
| Luchador contra la pared derecha: tiles en el borde izquierdo | el recorte de sprites que cruzan la X 512 los volvía a dibujar por la izquierda | `draw_sprite_tile_wrap` ya no repite el sprite |

Pendiente: probarlo a mano; el cambio de los sprites es un ajuste a ciegas (no se vio el fallo en las capturas de stage completo).
