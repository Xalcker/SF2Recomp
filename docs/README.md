# Documentación del proyecto SF2Recomp

- `PLAN.md`: hoja de ruta hacia código portable, por hitos.
- `DECISIONES.md`: decisiones de diseño numeradas.
- `REFERENCIAS.md`: enlaces y materiales de referencia.
- `../README.md`: cómo construir y usar el proyecto en macOS, Linux, Windows y Web.
- `REVISIONES.md`: revisiones del juego, chip CPS-B de cada una y por qué World 910522.
- `MODS.md`: **catálogo de todo lo que el port añade al juego** (pantalla ancha, HUD, lógica ancha, ultraancho, HFR, herramientas de desarrollo, entrenamiento, visores) y cómo se activa cada cosa.
- `MENU.md`: el menú del juego (tecla M), página por página.
- `WIDESCREEN.md`: pantalla ancha real (512 px, 16:9 sin estirar), HUD en los bordes y lógica ancha: cómo está hecha, qué se midió y los límites.
- `ULTRAWIDE.md`: stage completo con cámara fija para monitores ultraanchos (`mods/*.ini`).
- `HFR.md`: más de 60 cuadros por segundo por interpolación de render.
- `ENTRENAMIENTO.md`: el mod de entrenamiento (sin tiempo, dummy con guion, visor de entradas, cajas); `HITBOXES.md`: formato de las cajas de golpe y de daño.
- `HERRAMIENTAS_DEV.md`: los menús de desarrollo ocultos de Capcom y cómo abrirlos.
- `MEJORAS.md`: ideas de mods y mejoras, con el estado de cada una.
- `bitacora/`: una entrada por paso de trabajo, con fecha e hito.

Para registrar un paso nuevo: copiar el formato de la última entrada de
`bitacora/`, anotar comandos y resultados, y actualizar `PLAN.md` si el hito
cambia de estado.
