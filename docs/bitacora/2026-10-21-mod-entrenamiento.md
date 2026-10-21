# 2026-10-21 · Mod de entrenamiento (primera versión)

## Pregunta de partida

Empezar el modo entrenamiento de `docs/MEJORAS.md` sin esperar a la capa de mods completa: sin límite de tiempo, J2 como en un versus pero sin
controles y movido por un guion, visor de entradas y cajas de golpe conmutables, siempre con el mod apagado = juego original.

## Qué se hizo

- Direcciones de RAM halladas con `libsf2py`: temporizador `0xFF8ACE` (BCD) y su subcontador `0xFF8ACF` (30 → 1; si solo se fija el temporizador el marcador se
  queda en 98). Para entrar en un versus basta START1+START2 a la vez en el título con 2 créditos.
- `runtime/training.c`: gancho de VBlank (tiempo, barras, dummy con guion, versus automático, historial de entradas, cajas). Cajas: especificación de un
  subagente (`docs/HITBOXES.md`, `tools/hitbox_probe.py`), verificada por él con capturas; aquí se comprobó con `--shot-png` (cuadros 1950 y 2010,
  Ryu contra Ken con hadoken).
- Aplicación: sección `[training]`, teclas `7`/`8`/`9`, opciones `--training`, `--dummy`, `--dummy-script`, `--show-inputs`, `--show-hitboxes`, `--shot-png`.

- Menú del juego: página *Entrenamiento* (`PG_TRAINING`, entre Controles y las asignaciones, que pasan a las páginas 6-9 de `--menu-png`); `--menu-test` actualizado (una opción más en la página principal).

## Verificación

- `make test`: 8 de 8, con la prueba nueva `test_training` en `sf2_rom_tests` (41.753 comprobaciones).
- `make compare-video`: 17 de 17 cuadros idénticos a MAME con el mod apagado.
- Capturas de la ventana con visor de entradas y cajas; el temporizador se queda en 99.

## Pendiente

Menú del juego, reinicio de posiciones, grabar y reproducir entradas, medidor de daño, cajas con `stage_full`, y distinguir bloqueo y agarre (ver `docs/ENTRENAMIENTO.md`).
