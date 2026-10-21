# 2026-10-20 · Mod HFR · Fase 2: anfitrión

Cambios: `app/main.c` (`hfr_update`, `read_refresh`, ritmo propio sin vsync, un cuadro por vuelta, `--hfr`, `--stats`), `platform/config.[ch]` (`hfr`, tecla `hfr` = F1).
Reglas, medidas y el hallazgo del vsync a 60 Hz en `docs/HFR.md`. Apagado (por omisión) el anfitrión no cambia: `make test` y `make compare-video` (17 de 17) iguales.
