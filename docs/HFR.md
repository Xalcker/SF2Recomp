# HFR · más de 60 cuadros por segundo (tercer mod, solo de presentación)

Fecha: 2026-10-20. Estado: **implementado (fases 0 a 2) y probado jugando en un MacBook de 120 Hz** (correcciones en §5); activar con **F1**, `hfr = true`, `--hfr` o el menú **M**. Pendiente: la versión Web y una revisión sostenida a 240 Hz. Resumen de todos los mods en `MODS.md`. Regla de siempre: con el mod apagado, juego idéntico a MAME.

## 1. Qué se puede y qué no

- **Subir la velocidad de la lógica no sirve.** El juego avanza un paso por VBlank (59,637 Hz, `SF2_FRAME_HZ`); ir más rápido acelera el juego, no lo suaviza.
- **Lo viable es interpolar la imagen.** La lógica sigue a su ritmo y el anfitrión dibuja varios cuadros entre dos estados consecutivos. Es un mod solo de render: el estado de la máquina (CRC de las 14 secuencias) es el mismo con el mod encendido o apagado, así que no cambia la jugabilidad ni el audio.
- **Coste: un cuadro de latencia** (16,8 ms), porque se dibuja entre el estado anterior y el actual. Se compensa si el monitor es de 120 Hz o más; es el precio de interpolar en vez de extrapolar.

## 2. Qué se midió (Ryu contra Blanka, 520 cuadros de combate, tabla de objetos y registros de scroll de cada VBlank)

| Dato | Resultado |
|---|---|
| Entradas de la tabla de objetos no vacías | 169 por cuadro de media |
| Misma ranura con el mismo código y atributos en el cuadro siguiente | 82 % (60.502 de 74.216) |
| De esas, las que no se mueven (marcador, escenario) | 83 % |
| De las que se mueven, desplazamiento por cuadro | 1-4 px en el 94 %; 16-20 px en unas pocas (saltos y golpes) |
| Ranuras que cambian de código o atributos (animación o reasignación) | 18 %: no se interpolan, se toma el valor del cuadro nuevo |
| Cámara (SCROLL1-3 X) | 0 px en el 84 % de los cuadros, 1-6 px en el resto |

Conclusión: la identidad de ranura es lo bastante estable para interpolar posición; lo que cambia de forma se queda como está. Con movimientos de 1-4 px por cuadro y píxeles enteros, a 120 Hz la ganancia es sobre todo en cámara y movimientos rápidos; a 240 Hz se ven pasos de medio píxel en lo lento (hay que redondear, nunca difuminar).

## 3. Fases (hechas)

### Fase 0 · Estado anterior y actual (HECHA)
`Video` guarda dos instantáneas por VBlank: tabla de objetos, X/Y de las tres capas, desplazamiento de filas (`rowscroll`) y paleta. La RAM de tiles se usa siempre la actual.
**Hecho cuando:** con `t = 1` el cuadro es idéntico, píxel a píxel, al de hoy (prueba nueva en `rom_tests.c`).

Resultado: `Video.prev` y `Video.cur` (`VideoSnap`: tabla de objetos, X/Y de SCROLL1-3, desplazamiento de filas); en cada VBlank `prev = cur` y se captura la nueva.
**Hallazgo que cambió el diseño:** el manejador de IRQ2 corre dentro del propio VBlank, antes de que `sf2_run_frame` devuelva el control, y escribe los
registros y la RAM del cuadro siguiente. Volver a dibujar con la máquina "en vivo" daba un cuadro distinto en 392 de 6.000 casos. Por eso `snap_capture`
congela también la RAM gráfica (192 KiB), la paleta, los registros de CPS-A/B y 4 KiB de la RAM de trabajo en `Video.fz`, y el renderizador lee solo de ahí
(`video_render_snap(v, &st)`). Coste medido con `--bench-mode video`: unos 2.550 cps contra 2.600, es decir, menos de un 2 %.
API de pruebas: `sf2_video_snapshot(s, 0|1, obj, scroll)` y `sf2_render_snapshot(s)`. Verificación: `test_hfr_snapshot` (3.000 cuadros con 384 y con 512 px: la
instantánea anterior es la actual del cuadro previo y el redibujado desde la actual es idéntico), `make test` 8 de 8 y `make compare-video` 17 de 17 con MAME.

### Fase 1 · Dibujar en `t` (0 a 1) (HECHA)
- Capas: scroll de cada una = anterior + (actual − anterior) · t, redondeado; si el salto pasa de 64 px (corte de escena, cambio de cámara) se toma el actual. `rowscroll` fila a fila con la misma regla.
- Sprites: si ranura, código y atributos coinciden y el desplazamiento es ≤ 32 px, X/Y interpolados; si no, el valor actual. Los de la franja del marcador (ver `hud_side`) se quedan fijos.
- Paleta (fundidos): la actual.
**Hecho cuando:** capturas a `t = 0,25 / 0,5 / 0,75` de un salto de Ryu y de un desplazamiento de cámara sin sprites duplicados ni fantasmas.

Resultado: `video_render_t(v, t256)` / `sf2_render_interp(s, t256)`; `t256 = 256` es el cuadro normal. Se interpolan la X/Y de SCROLL1-3 (se toma el cuadro actual si el salto pasa de 64 px),
el rowscroll (ahora también en la instantánea: `VideoSnap.rows`, solo si el desplazamiento de la tabla no cambió; salto máximo 32) y los sprites con la misma ranura, código y
atributos (X con signo en el stage completo, 9 bits en los demás; salto máximo 12 px, ver abajo). El redondeo es al píxel entero. Tras un cambio de modo o una carga de estado
(`video_render` fuera del VBlank) `prev = cur`, así que ese intervalo no se interpola.
Comprobación: capturas de Ryu saltando contra Blanka a `t = 0, 0,25, 0,5, 0,75, 1` (Ryu y la cámara avanzan de forma gradual, sin duplicados ni fantasmas) y `test_hfr_snapshot` ampliada: con `t = 256` el cuadro es el normal,
el cuadro a `t = 0,5` es estable al repetirlo y distinto del normal en 1.431 de 3.000 cuadros, y dibujar intermedios no cambia el CRC de estado de la partida
(comparado con una máquina de control sin ellos). `make test` 8 de 8, `make compare-video` 17 de 17.

### Fase 2 · Anfitrión (HECHA)
- El dibujado pasa a seguir el refresco del monitor, no el VBlank del juego: en cada refresco se calcula `t` con el mismo reloj que ya marca el audio (el anfitrión ya hace `audio_queued_ms`). Así sirve cualquier monitor (120, 144, 165, 240 Hz) sin exigir múltiplos de 59,637.
- Config `hfr = off | auto | N`, tecla propia, `--hfr`; `off` es el camino de hoy sin tocar.
- Web (Emscripten): `requestAnimationFrame` ya da el refresco; queda para después.
**Hecho cuando:** 120 Hz fijos sin tirones de reloj durante 10 minutos con sonido.

Resultado (`app/main.c`, `platform/config.[ch]`): `hfr = true` en `[video]`, tecla **F1** (`hfr` en `[hotkeys]`), `--hfr`. En cada refresco `hfr_update` calcula `t = (ahora − llegada del último cuadro) / periodo`
(0 a 256) y llama a `sf2_render_interp`; se enseña un cuadro del juego por detrás del emulado (latencia de un cuadro) y cada cuadro se enseña una vez.
- **Un cuadro por vuelta:** con HFR el bucle emula como mucho un cuadro por refresco (más solo si la cola de audio baja de 12 ms), para no saltarse cuadros intermedios: antes el 22 % de las vueltas emulaban 2 o más.
- **Monitor de 60 Hz:** si el refresco, o lo que de verdad se presenta por segundo, es menor de 90 Hz, `t = 256` siempre: se enseña el cuadro normal, sin latencia de más.
- **Hallazgo en un MacBook de 120 Hz:** con vsync, `SDL_RenderPresent` tardaba 14,7 ms de media (la presentación iba a 60 Hz aunque SDL anuncia 120). Si pasa, se apaga el vsync y se sigue el refresco con un reloj propio
  (`soft_pace`, aviso en pantalla); medido: 120 presentados por segundo y `SDL_RenderPresent` de 1,2 ms. Sin vsync (`vsync = false`) pasa lo mismo. Apagar el HFR devuelve el vsync.
- Con `--stats`: monitor, presentados por segundo, tiempo medio de `SDL_RenderPresent` y reparto de `t` en octavos. Sonido activo: 0 huecos de audio en 900 cuadros, cola entre 4 y 78 ms.
- El título de la ventana añade `(HFR n Hz)` con los dibujados por segundo.
- Pendiente de ojos: ver a 120 Hz si el movimiento es más suave y si hay saltos en cambios de escena (fase 3).

### Fase 3 · Pruebas
Con `hfr` encendido: CRC de estado de las 14 secuencias iguales a las de hoy; `make compare-video` 17/17 con el mod apagado; mod combinado con widescreen y ultraancho (el ancho no afecta a la interpolación).

## 4. Riesgos
- **Parpadeos del propio juego** (sombras y efectos que alternan cuadros): se verán igual; interpolar esas ranuras las movería. Hay que comprobarlo en golpes y fuego.
- **Cortes de escena y cambios de ranura:** el umbral de 32 px y la comparación de código lo cubren; hay que mirar el cambio de ronda y los KO.
- **Latencia:** un cuadro de lógica más; opción de apagar la interpolación de la cámara si se nota.
- **Rendimiento:** dibujar a 240 Hz cuesta el render a 768 px (hoy unos 2.900 cps en total con la CPU); medir con `--bench-mode video`.

## 5. Correcciones tras la prueba de juego (2026-10-20)

- **Parpadeo en la barda del stage de Ryu (solo con HFR):** una hilera de 12 sprites idénticos (código 0x8605) a y = 128 se corre 16, 32 o 48 px de un cuadro al otro (la lista se desplaza dentro de las ranuras).
  Emparejada por ranura con umbral de 32 px, la hilera se deslizaba y parpadeaba en los cuadros intermedios. Se encontró simulando la secuencia a 120 Hz (t = 0,4 y 0,9 alternados) y mapeando los píxeles que alternan A-B-A por capa: el
  borde de la barda salía solo en la capa de sprites. El umbral de sprites baja a **12 px** (el 98 % de los movimientos medidos son menores); los saltos mayores se enseñan sin interpolar, como antes. Con el cambio la franja desaparece del mapa.
