# 2026-10-20 · Ultrawide · Stage completo con cámara fija

Copia de fuentes `SF2Recomp-ultrawide` (sin `build/`, con enlaces a `external/`, `sf2/` y `sf2.zip`); no es un repositorio git. Cambios solo aquí.

## Medidas (build original, sin cambiar nada)
- Recorrido de la cámara: `0x9D0 = 0x100`, `0x9D2 = 0x280` en las 12 pantallas de lucha; X de los luchadores de 296 a 984 (mundo de 768 px).
- La X de los sprites está en 0-511 en todos los cuadros medidos: el constructor `0x7F24E` descarta (sin signo) lo que cae fuera; no hay máscara de 9 bits.
- Pantalla = X de mundo - cámara (`0xBD8`) + 0x40; las ventanas de actividad y visibilidad están en `0x2816`-`0x2948`, `0x47DC`-`0x4830`.

## Qué se hizo
36 parches en `recompiler/mod_patches.txt` (D58 + D59), `mod_fixcam` en la CPU, `sf2_set_stage_full`, `Video.xsigned` y relleno hasta 800 px, config
`stage_full`, `--stage-full`, tecla F4, `ultrawide.ini` (768) y `ultrawide-3440.ini` (688, ajuste exacto 3440x1440 con `aspect = 4:3`).

## Resultados
| Comprobación | Resultado |
|---|---|
| `make test` / `make compare-video` | 8 de 8 / 17 de 17 |
| 14 secuencias con `stage_full` apagado, `playgen.py --verify` | CRC de estado iguales a la grabación; secuencias sin modificar |
| Combate a 768 con la cámara fija (Blanka, Guile, Honda, Dhalsim) | escenario entero, sprites a los bordes; los elefantes de Dhalsim completos |
| Ryu hasta cada pared | X 296 y 984 (768) o 336 y 944 (688), cámara fija en 448 |
| Bonus (coche, barriles) | funcionan; los objetos aparecen donde los pone el juego |
| Rendimiento (`--bench`) | 2.881 cps (vídeo) |

## Pendiente
Revisar a mano IA, jefes, y escenarios que no se pudieron ver en el barrido (Ken, Zangief, Balrog, Vega con la cámara fija), y decidir el relleno del arte
lejano si molesta el reflejo. Falta que el usuario lo juegue en el monitor ultraancho.
