# Ultraancho · stage completo con cámara fija (segundo mod, tras el widescreen)

Estado: probado a mano en 3440x1440 el 2026-10-20 (D59 y `docs/bitacora/2026-10-20-ultrawide-*.md`). Cambia el juego: con `stage_full = false` todo es la recreativa.

```bash
make
build/sf2 --config mods/ultrawide-3440.ini
```

- `mods/ultrawide-3440.ini`: cuadro de 688 px, ajuste exacto de 3440x1440 (con `aspect = 4:3`, sin franjas). Los luchadores llegan a 40 px de cada extremo.
- `mods/ultrawide.ini`: cuadro de 768 px, el escenario completo; deja franjas finas arriba y abajo en 3440x1440.
- No tocan tu `sf2recomp.ini` de preferencias. Teclas: **F4** stage completo, **F9** pantalla ancha, **F3** lógica ancha, **F11** pantalla completa. También `--stage-full`.
- Contra Dhalsim no se dibujan los elefantes de primer plano (SCROLL1 y SCROLL3).
- Pendiente: IA, jefes y revisión de Zangief, Balrog y Vega con cámara fija; el bonus del coche tiene el yate de la derecha con el casco cortado por el reflejo.

- 2026-10-20, tras jugarlo: en SCROLL3 el reflejo del arte no pasa de 2 tiles (64 px, el margen del widescreen de 512). En Blanka el reflejo de 192 px dibujaba un tramo de árbol y raíces suelto a la izquierda; ahora esa zona queda con el suelo y la cabaña de SCROLL2 a la vista.
