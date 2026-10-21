# 2026-10-20 · Mod HFR · Fase 0: instantáneas de vídeo

Cambios: `runtime/video.[ch]` (`VideoSnap`, `Video.prev/cur/fz`, `video_render_snap`), `runtime/sf2.[ch]` (`sf2_video_snapshot`, `sf2_render_snapshot`), `tests/rom_tests.c` (`test_hfr_snapshot`).
Detalle, medidas y el hallazgo del IRQ2 dentro del VBlank en `docs/HFR.md`. Sin mod activo no hay interruptor todavía: la fase 0 no cambia ningún cuadro (17 de 17 con MAME).
