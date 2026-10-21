# Mod de entrenamiento

Primera versión (2026-10-21) del modo entrenamiento de `docs/MEJORAS.md` (ideas 1, 2 y parte de la 5). Es un mod conmutable: **apagado,
el juego es idéntico a MAME** (`make compare-video` 17/17, `make test` completo). Cambia la partida solo mientras `enabled = true`.

## Qué hace

| Función | Detalle |
|---|---|
| Sin límite de tiempo | Cada VBlank del combate escribe `0xFF8ACE = 0x99` (temporizador BCD) y `0xFF8ACF = 30` (cuadros hasta el siguiente segundo; sin esto el marcador se queda en 98). |
| Barras que se recuperan | `refill = delay`: la vida (`jugador+0x30`, máximo 0x90) se llena tras `refill_delay` cuadros sin cambiar. `always`: siempre llena, nunca hay KO. No resucita a un KO. |
| J2 sin controles | `lock_p2`: se descartan las entradas de J2 del anfitrión (teclado y mando); las del dummy entran en su lugar. J1 y las monedas no cambian. |
| Dummy | `stand`, `crouch`, `jump`, `block` / `block_crouch` (se cubre cuando J1 pulsó un ataque en los últimos 30 cuadros), `walk_forward`, `walk_back` y `script`. |
| Guion del dummy | Pasos `botones:cuadros` en bucle, p. ej. `d:30 df:3 f+lp:2 n:30` (agachado 30, abajo-delante 3, delante+puñetazo débil 2, nada 30). `n f b u d df db uf ub` (relativas al rival), `l r` (absolutas), `lp mp hp lk mk hk`; `+` pulsa varios. Hasta 64 pasos, 1-3600 cuadros cada uno. Un guion malo se rechaza con el motivo y no cambia nada. |
| Versus automático | `auto_vs`: en el título mete monedas hasta tener 2 créditos y pulsa START1+START2 a la vez; en la selección J2 confirma el luchador del cursor. Al acabar la partida vuelve a empezar solo. |
| Visor de entradas | Palanca y seis botones en vivo y historial (más reciente abajo: dirección en notación numérica absoluta 1-9, botones y cuadros que se mantuvo) de J1 (izquierda) y J2 (derecha). Lee lo que recibe el juego, no el teclado. |
| Cajas de golpe y de daño | Azul = cabeza/cuerpo/pies, amarillo = punto débil, rojo = ataque, verde = empuje, naranja = ataque de proyectil, gris = empuje de proyectil. Formato y direcciones en `docs/HITBOXES.md`. |

## Cómo se usa

- `build/sf2 --training` (enciende el mod y el visor de entradas), `--dummy MODO`, `--dummy-script "…"`, `--show-inputs`, `--show-hitboxes`.
- Teclas (cambiables en `[hotkeys]`): `7` mod de entrenamiento, `8` visor de entradas, `9` cajas.
- Menú del juego (**M** ▸ *Entrenamiento*): todas las opciones de abajo, sin reiniciar; el guion del dummy solo se edita en el `.ini` (`script`). **Guardar configuración** las deja en el `.ini`. Ver `docs/MENU.md`.
- Sección `[training]` de `sf2recomp.ini` (valores por omisión: tiempo infinito, barra tras 60 cuadros, J2 bloqueado, versus automático, dummy `stand`).
- Con el mod encendido por tecla en mitad de una partida, el versus automático solo actúa en el título; el dummy y las barras valen de inmediato.

## Cómo está hecho

- `runtime/training.c` (+ `training_priv.h`, `training.h`): un gancho en el VBlank (`on_vblank` de `sf2.c`), justo antes de entregar la IRQ2, retoca la RAM y calcula las entradas del cuadro siguiente (`cps.manual_in*`). Determinista; fuera del estado guardado.
- `sf2.h`: `sf2_training_set/get`, `sf2_training_history`, `sf2_training_pad`, `sf2_training_boxes`. El núcleo no dibuja: entrega datos y `app/main.c` los pinta con la resolución de la ventana (`draw_inputs`, `draw_hitboxes`).
- `tests/rom_tests.c::test_training`: apagado (y con solo el registro de entradas) el estado es idéntico; guiones válidos e inválidos; versus automático hasta el combate con tiempo y barras fijos; J2 ignora al anfitrión y sigue el guion.
- `tools/sf2py.c`: `sf2py_training` para probarlo desde Python.
- `--shot-png RUTA:CUADRO` guarda la ventana con los visores (con `SDL_VIDEO_DRIVER=dummy`) para comprobar el dibujo sin pantalla.

## Límites conocidos y siguiente

- Las cajas con pantalla ancha se centran sumando medio ancho extra; con `stage_full` el desplazamiento correcto sin resolver. Agarre y bloqueo no tienen caja propia (ver `docs/HITBOXES.md`).
- Dirección del visor absoluta (no relativa a la orientación). El dummy `block` se basa en los botones de J1, no en que el ataque esté activo; con el mapa de cajas se podría usar «ataque activo» (byte `+0xC` de la pose).
- El guion del dummy no se edita desde el menú (solo en el `.ini`). Faltan reinicio de posiciones, grabación y reproducción de entradas, y medidor de daño y combo.
- El dummy elige al luchador que tenga el cursor de J2 al empezar la selección.
