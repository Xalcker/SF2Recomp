# Ultraancho · stage completo con cámara fija (segundo mod, tras el widescreen)

Estado: **implementado y probado a mano en 3440x1440** (2026-10-20, D59 y `docs/bitacora/2026-10-20-ultrawide-*.md`). Cambia el juego: con `stage_full = false` todo es la recreativa. Resumen de todos los mods en `MODS.md`; la base (ancho del cuadro, relleno, HUD) en `WIDESCREEN.md`.

## Qué es

Las 12 pantallas de lucha comparten el recorrido de la cámara (topes `0x9D0 = 0x100`, `0x9D2 = 0x280`), así que el mundo mide **768 px**. Con `stage_full` la cámara se fija en el centro de ese recorrido, el combate se ve entero sin scroll y los luchadores recorren todo el escenario; el cuadro de 768 px lo muestra completo y el de 688 px (3440x1440 con `aspect = 4:3`) deja 40 px sin ver por lado. Implica la lógica ancha (D58) y añade parches de cámara, de las ventanas «¿está a la vista?» de objetos y proyectiles y del constructor de sprites (que admite X con signo fuera de los 9 bits); todo en `recompiler/mod_patches.txt` (D59). `mod_wide` y `mod_fixcam` valen solo mientras la escena dibujada es de las que se ensanchan (se fijan en cada VBlank): en los menús el juego corre sin parches. No tiene estado guardado propio: se puede alternar en caliente.

## Uso

```bash
make ROMDIR=/ruta/a/sf2.zip -j8
build/sf2 --config mods/ultrawide-3440.ini
```

- `mods/ultrawide-3440.ini`: cuadro de 688 px, ajuste exacto de 3440x1440 (con `aspect = 4:3`, sin franjas). Los luchadores llegan a 40 px de cada extremo.
- `mods/ultrawide.ini`: cuadro de 768 px, el escenario completo; deja franjas finas arriba y abajo en 3440x1440.
- No tocan tu `sf2recomp.ini` de preferencias. Teclas: **F4** stage completo, **F9** pantalla ancha, **F3** lógica ancha, **F11** pantalla completa. También `--stage-full` y, en el menú **M**, el ajuste rápido «Ultraancho 3440x1440» o «Ultraancho 768».
- Contra Dhalsim no se dibujan los elefantes de primer plano (SCROLL1 y SCROLL3).
## Límites y pendiente

- Pendiente: IA, jefes y revisión de Zangief, Balrog y Vega con cámara fija; el bonus del coche tiene el yate de la derecha con el casco cortado por el reflejo.

- 2026-10-20, tras jugarlo: en SCROLL3 el reflejo del arte no pasa de 2 tiles (64 px, el margen del widescreen de 512). En Blanka el reflejo de 192 px dibujaba un tramo de árbol y raíces suelto a la izquierda; ahora esa zona queda con el suelo y la cabaña de SCROLL2 a la vista.
