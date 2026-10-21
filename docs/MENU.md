# Menú en el juego

Se abre con **M** (tecla `menu` en `[hotkeys]`) o con el botón guía del mando; pausa el juego. Flechas o cruceta para elegir y cambiar, **Enter** o A para aceptar, **Esc** o B para volver a la página anterior (en la principal, cierra); **M**, Start o el botón guía cierran de golpe.

## Páginas
**Principal:** Continuar · Ajuste rápido (Original, Widescreen 16:9, Ultraancho 3440x1440, Ultraancho 768) · Pantalla ancha y mods ▸ · Video ▸ · Sonido ▸ · Controles ▸ · Guardar configuración · Reiniciar el juego · Salir.

| Página | Opción | Equivale a |
|---|---|---|
| Pantalla ancha y mods | Pantalla ancha | F9, `widescreen` |
| | Ancho del cuadro (512, 576, 640, 688, 768, 800 px) | `wide_width` |
| | Marcador (centro / bordes) | `hud` |
| | Lógica ancha (cambia el juego) | F3, `wide_logic` |
| | Stage completo (cambia el juego) | F4, `stage_full` |
| | HFR | F1, `hfr` |
| Video | Escala de la ventana (x1 a x6, o ajustar = maximizada) | `scale` |
| | Pantalla completa | F11, `fullscreen` |
| | Escala entera | `integer_scale` |
| | Relación de aspecto (4:3 / pixel) | `aspect` |
| | Filtro suave | `smooth` |
| | VSync | `vsync` |
| Sonido | Sonido (silencia la sesión) y Volumen (de 10 en 10) | `audio`, `volume` |
| Controles | Teclado jugador 1 y 2, Mando jugador 1 y 2, Restaurar todos los controles | `[keys]`, `[pad]` |

**Asignar controles:** en Controles ▸ elige jugador y dispositivo; cada acción (arriba, abajo, izquierda, derecha, seis botones, moneda, start y, para el jugador 1, servicio) muestra su tecla o botón.
Enter y pulsa la tecla o el botón (los gatillos también valen); **Esc** cancela y **Supr** o Retroceso la deja sin asignar. "Restaurar esta página" devuelve los valores por omisión. Un cambio sustituye todas las asignaciones de esa acción por la nueva.

Los cambios valen para la sesión; solo "Guardar configuración" escribe el `.ini` en uso. Cada mod sigue siendo un interruptor: apagados, el juego es idéntico a MAME.

## Pruebas sin tocar el teclado
- `sf2 --menu-png archivo.png [--menu-page N]` abre el menú tras 400 cuadros, guarda una captura y termina (N: 0 principal, 1 mods, 2 video, 3 sonido, 4 controles, 5 y 6 teclado 1 y 2, 7 y 8 mando 1 y 2).
- `sf2 --menu-test` teclea una secuencia (asignar teclas, escala, VSync) y comprueba el resultado.
