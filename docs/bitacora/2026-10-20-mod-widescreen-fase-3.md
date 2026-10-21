# 2026-10-20 · Mod widescreen · Fase 3: HUD pegado a los bordes

## Pregunta de partida

¿Se puede acercar el marcador del combate a los bordes de la ventana ancha sin tocar a los luchadores ni la partida?

## Hallazgo que cambió el plan

El plan decía «mitad izquierda y derecha de SCROLL1». No es así: SCROLL1 solo tiene una banda gris; el marcador (puntuaciones, nombres, tiempo, KO,
barras de vida y marcadores) son sprites. Volcando la tabla de objetos en la presentación, la pelea, la victoria y el recuento
(`ryu.txt` cuadros 1450, 2150, 2300, 9250, 9460) las ranuras son fijas: 0-13 puntuaciones, 14-25 tiempo y récord, 26-36 y 37-43 nombres, 44-63
barras, 72-75 KO y marcadores (código 0x8302), 64-71 mensajes centrales, 76+ luchadores y efectos.

## Qué se hizo

- `runtime/video.[ch]`: `hud_side` (por ranura, centro y código) y `Video.hud_edges`; `render_sprites` desplaza la X de la entrada ±(w-384)/2.
- `runtime/sf2.[ch]`: `sf2_set_hud_edges`. `platform/config.[ch]`: `hud = center | edges`. `app/main.c`: `--hud-edges`.
- Primer intento con las ranuras 72-75 enteras: el texto grande «ROUND 1» se partía (sus letras caen ahí); se restringió a los marcadores por
  código 0x8302.
- `tests/rom_tests.c`: tercera máquina con la opción activa; fuera de las 48 primeras filas el cuadro es idéntico al de HUD centrado.

## Resultados

| Comprobación | Resultado |
|---|---|
| Filas 48-223 con `hud = edges` frente a `center` (`ryu.txt` y `versus.txt`, 42.000 cuadros cada una) | idénticas, 0 cuadros distintos |
| Filas 0-47 | distintas en 29.016 y 13.323 cuadros (el marcador se mueve); hay tinta nueva en las 8 primeras columnas |
| Centro de 384 columnas con el HUD centrado | sigue idéntico (0 líneas distintas) |
| `make test` / `make compare-video` | 8 de 8 / 17 de 17 idénticos a MAME |
| Revisión visual (presentación, pelea, victoria, recuento, dos jugadores, salto de Ryu) | marcador intacto, barras en los bordes, KO y tiempo en el centro |

Queda un hueco de 64 px entre cada barra y el KO (no se estiran). Con las barras en los bordes la barra del 2P agota su vida hacia el KO igual que antes.
Se vio además una costura menor en el fondo izquierdo de Blanka durante la presentación (herencia de la fase 2).

## Pendiente

Fase 4 (lógica ancha), opcional. No se probó la versión Web ni se repitió `compare-coverage`.

## Ajustes tras la prueba del usuario (D56)

- La demostración no se ensanchaba: el criterio de escena solo cubría el modo 2. Ahora cubre modo 0x0A (fase 2, subfase 2) y 0x0C (fase 2).
- El «INSERT COIN» del demo se partía con `hud = edges`: se excluyen los sprites con y >= 80.
- Elefantes de Dhalsim y barco de Ken cortados en el extremo: el arte no existe más allá; ningún ajuste del relleno lo mejora.
- Comprobado con `validacion.txt` (12.000 cuadros, incluye demo), `ryu.txt` y `versus.txt` (42.000): centro idéntico, 0 cuadros distintos bajo
  la banda del marcador; `make test` 8 de 8 y `compare-video` 17 de 17.
