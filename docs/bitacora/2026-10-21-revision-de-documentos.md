# 2026-10-21 · Revisión de la documentación tras los mods

## Pregunta de partida

`docs/WIDESCREEN.md` se leía como un plan (esfuerzos, «decisiones que conviene confirmar», fases «propuestas») aunque el mod está hecho, y ni el README ni los de cada
carpeta decían nada del menú, los mods posteriores al hito 6 ni las opciones nuevas.

## Qué se hizo

- Nuevo `docs/MODS.md`: catálogo de todos los mods (qué hacen, si cambian el juego, cómo se activan, documento y límites).
- `WIDESCREEN.md` reescrito como documentación de lo implementado (cómo se usa, cómo está hecho por fases, medidas de viabilidad como antecedente, pruebas, límites);
  `ULTRAWIDE.md` y `HFR.md` con su estado real; `MEJORAS.md` con una tabla del estado de cada idea.
- READMEs de la raíz, `app/`, `platform/`, `runtime/`, `tests/` y `tools/`: opciones de línea de comandos, teclas, secciones del `.ini`, módulos y pruebas nuevos.
- `PLAN.md` (sección «Después del hito 6»), `DECISIONES.md` (D60, mod de entrenamiento) e índice `docs/README.md` completo.

## Verificación

Enlaces entre documentos comprobados con un script; los datos (D53-D59, opciones de `config.c`, teclas, ajustes rápidos del menú) se contrastaron con el código.
