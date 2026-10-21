# Cambios locales a Musashi 4.60 (commit 313ebf1, MIT)

Musashi solo se usa en los arneses de validación y en las herramientas (D13); el juego no lo lleva.

| Fecha | Archivo | Cambio | Motivo |
|---|---|---|---|
| 2026-10-19 (hito 6) | `m68k_in.c` | `1 << (DX & 0x1f)` y `1 << (OPER_I_8() & 0x1f)` pasan a `1u << …` en BTST/BCHG/BCLR/BSET de 32 bits (8 sitios) | con el bit 31, `1 << 31` sobre `int` es comportamiento indefinido; UndefinedBehaviorSanitizer detenía `lockstep_vs_musashi` (bitácora `2026-10-19-hito6-sanitizadores-y-ejecucion-larga.md`). El resultado en la práctica era el mismo. |
