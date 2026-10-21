# Mods del port

Catálogo de lo que el port añade al juego original. Todo está implementado y se maneja desde el menú del juego (**M**, `docs/MENU.md`), con teclas,
con opciones de línea de comandos o desde `sf2recomp.ini`. Última revisión: 2026-10-21.

**Regla de todos los mods:** son interruptores y, **apagados, el juego es idéntico a MAME** (`make compare-video` 17/17, las 14 secuencias con el mismo CRC de
estado, `make test`). Se distinguen dos clases:

- **Solo presentación** (no tocan la partida; el estado del juego es el mismo encendidos o apagados): pantalla ancha, HUD en los bordes, HFR, visores de entradas y de cajas.
- **Cambian el juego** (con el mod encendido la partida ya no coincide con MAME): lógica ancha, stage completo, herramientas de desarrollo, modo entrenamiento.
  Ninguno forma parte del estado guardado, así que un estado guardado con uno puede cargarse con otro.

| Mod | Qué hace | Cambia el juego | Cómo se activa | Documento |
|---|---|:---:|---|---|
| Pantalla ancha | Más escenario a los lados en el combate (384 a 800 px; 512 = 16:9 con los píxeles de la recreativa), sin estirar | no | **F9**, `widescreen = true` + `wide_width`, `--widescreen`, menú ▸ Pantalla ancha y mods | `WIDESCREEN.md` |
| HUD en los bordes | El marcador del combate se pega a los lados de la ventana ancha | no | `hud = edges`, `--hud-edges`, menú | `WIDESCREEN.md` |
| Lógica ancha | Los luchadores y la cámara usan todo el ancho de la ventana ancha | **sí** | **F3**, `wide_logic = true`, `--wide-logic`, menú | `WIDESCREEN.md` |
| Stage completo (ultraancho) | Cámara fija y escenario entero a la vista (688 o 768 px) para monitores 21:9 y más | **sí** | **F4**, `stage_full = true`, `--stage-full`, menú; perfiles `mods/ultrawide*.ini` | `ULTRAWIDE.md` |
| HFR | Cuadros intermedios (interpolados) al refresco del monitor: 120, 144, 240 Hz sin acelerar el juego | no | **F1**, `hfr = true`, `--hfr`, menú | `HFR.md` |
| Herramientas de desarrollo | Abre los menús ocultos de Capcom que el código ya trae (prueba de scroll y OBJ TEST) | **sí** | `--dev-tool 1\|2`, menú ▸ Pantalla ancha y mods | `HERRAMIENTAS_DEV.md` |
| Modo entrenamiento | Sin límite de tiempo, barras que se recuperan, J2 como dummy con guion, versus automático | **sí** | **7**, `[training] enabled = true`, `--training`, menú ▸ Entrenamiento | `ENTRENAMIENTO.md` |
| Visor de entradas | Palanca, botones e historial de J1 y J2 en pantalla | no | **8**, `show_inputs = true`, `--show-inputs`, menú ▸ Entrenamiento | `ENTRENAMIENTO.md` |
| Cajas de golpe y de daño | Hitboxes y hurtboxes de luchadores y proyectiles | no | **9**, `show_hitboxes = true`, `--show-hitboxes`, menú ▸ Entrenamiento | `ENTRENAMIENTO.md`, `HITBOXES.md` |

Las teclas se cambian en `[hotkeys]`. Además hay un **menú** (M) con ajuste rápido (Original, Widescreen 16:9, Ultraancho 3440x1440, Ultraancho 768), páginas por tema y
«Guardar configuración» (`docs/MENU.md`), estados guardados en 10 ranuras (F5/F7), capturas (F12) y remapeo de teclado y mando (`README.md`, `platform/README.md`).

## Pantalla ancha, HUD, lógica ancha y stage completo

Cuatro piezas que se apilan (`WIDESCREEN.md` y `ULTRAWIDE.md` tienen el detalle y las medidas):

1. **Ancho del cuadro** (D53). El renderizador entrega un cuadro de `wide_width` píxeles (par, 384 a 800) en vez de 384. Con `aspect = 4:3` y 512 la ventana sale 16:9. Solo el combate
   (y los bonus, y la demostración) se ensancha; título, selección, mapa, versus y final se dibujan en sus 384 originales centrados con barras del color del fondo (D57), de modo
   que la textura y la ventana no cambian con la escena. Invariante probado: las 384 columnas centrales del cuadro ancho son idénticas al cuadro de 384.
2. **Relleno de los extremos** (D54). El arte de cada escenario acaba antes que la cámara; el borde se lee de la RAM gráfica y se refleja fuera de la ventana original. Es una invención
   plausible, no arte de Capcom: junto al borde se ve una copia reflejada.
3. **HUD en los bordes** (D55, D56). Las ranuras de la tabla de objetos que forman el marcador se mueven ±64 px; el tiempo, el KO y los mensajes se quedan en el centro.
4. **Lógica ancha y stage completo** (D58, D59). Parches en el código recompilado (`recompiler/mod_patches.txt`, un campo `mod_wide` de la CPU que los apaga) cambian las paredes de los
   luchadores, las ventanas de actividad de los objetos y la cámara. Con `stage_full` la cámara queda fija en el centro del recorrido (768 px de mundo) y los luchadores lo recorren entero.

Perfiles listos: `mods/ultrawide-3440.ini` (688 px, ajuste exacto de 3440x1440) y `mods/ultrawide.ini` (768 px). Se usan con `build/sf2 --config mods/ultrawide-3440.ini` y no tocan tu
`sf2recomp.ini`.

## HFR (más de 60 cuadros por segundo)

La lógica sigue a 59,637 Hz (acelerarla cambiaría el juego); el anfitrión dibuja varios cuadros entre dos estados consecutivos interpolando posición de sprites y de capas, redondeada
al píxel. Cuesta un cuadro de latencia; con un monitor de 60 Hz no hace nada. Detalles, medidas y correcciones tras jugarlo en `HFR.md`.

## Modo entrenamiento y visores

Un gancho en el VBlank (`runtime/training.c`) retoca la RAM y las entradas justo antes de entregar la IRQ2: temporizador y su subcontador, barras de vida, entradas de J2 (dummy con guion),
monedas y START del versus automático. Los visores solo leen. `ENTRENAMIENTO.md` explica las opciones, la sintaxis del guion y los límites; `HITBOXES.md` el formato de las cajas.

## Herramientas de desarrollo de Capcom

El bloque `0x86E98-0x8A2C8` de la ROM son menús de desarrollo que la placa nunca abre; están recompilados y `--dev-tool` los abre con la combinación de entrada que el juego espera
(`HERRAMIENTAS_DEV.md`).

## Cómo se comprueba que apagado = original

- `make compare-video`: 17 cuadros píxel a píxel contra MAME.
- `python3 tools/playgen.py --verify tools/sequences/*.txt`: el CRC de estado de las 14 secuencias, con los parches de lógica apagados.
- `make test` (`tests/README.md`): invariantes de ancho (`test_widescreen`), HUD, estados con relleno, instantáneas de HFR (`test_hfr_snapshot`), lógica ancha y stage completo, y `test_training`.

## Límites conocidos

- Relleno de extremos: es un reflejo; los fondos con rótulos o figuras junto al borde muestran su copia. Dhalsim (elefantes de primer plano) y Ken (barco) no tienen arte más allá.
- Lógica ancha y stage completo: no se han revisado a fondo la IA, los jefes ni los ataques que empujan contra la pared; la dificultad de la IA no está reajustada.
- Cajas de golpe con `stage_full`: el desplazamiento no está resuelto. Agarre y bloqueo no tienen caja propia.
- HFR en la Web: pendiente. Firefox y Safari no probados (README).
- Entrenamiento: sin reinicio de posiciones, grabación de entradas ni medidor de daño; el guion del dummy se edita en el `.ini`.
- Stage completo: falta revisar a Zangief, Balrog y Vega con la cámara fija y el bonus del coche (el yate de la derecha tiene el casco cortado por el reflejo); contra Dhalsim no se dibujan los elefantes de primer plano.
