# 2026-10-19 · Hito 0 · Identificación del set por CRC y limpieza de ROMs

## Pregunta de partida

No estaba claro de qué versión de MAME venía el set, y había que dejar en
`sf2/` solo lo que usa la revisión objetivo (World 910522, decisión D2).

## Qué se hizo

1. CRC32 de los 84 archivos de `sf2/` (python3 con `zlib.crc32`).
2. Cotejo contra `ROM_START( sf2 )` en tres fuentes:
   - MAME actual: `src/mame/capcom/cps1.cpp` (rama master).
   - mame2003 (MAME 0.78): `src/drivers/cps1.c` del repo libretro/mame2003-libretro.
   - mame2000 (MAME 0.37b5): `src/drivers/cps1.c` del repo libretro/mame2000-libretro.
3. Movimiento de los archivos no usados a la Papelera de macOS, carpeta
   `~/.Trash/sf2_roms_no_usadas_2026-10-19/` (reversible, nada borrado).

## Resultados

- Los 23 CRC de la revisión World 910522 coinciden con los de MAME actual.
- Los nombres de archivo coinciden exactamente con **MAME 0.78 (mame2003)**:
  `sf2e.30g`, `sf2_29a.bin`, `sf2_09.bin`, `sf2_18.bin`, etc. Es un set de esa
  época, con clones mezclados (USA, Japón, otras revisiones World) y los
  archivos de sonido de Super Street Fighter II (CPS2), que no pertenecen aquí.
- MAME 0.37b5 (mame2000) usaba otros nombres (`sf2e_30b.rom`, `sf2gfx01.rom`)
  y su set `sf2` era la revisión 910214, no la nuestra.
- MAME actual usa nombres con posición de zócalo (`sf2e_30g.11e`,
  `sf2-5m.4a`, `sf2_9.12a`) y añade siete volcados de PLD (`buf1`, `ioa1`,
  `prg1`, `rom1`, `sou1`, `stf29.1a`, `iob1.11d`, `c632.ic1`) que este set no
  tiene.

### Archivos que quedan en `sf2/` (23)

| Función | Archivos |
|---|---|
| Programa 68000 (4 pares) | sf2e.30g, sf2e.37g, sf2e.31g, sf2e.38g, sf2e.28g, sf2e.35g, sf2_29a.bin, sf2_36a.bin |
| Gráficos (12 × 512 KB) | sf2_05, 06, 07, 08, 14, 15, 16, 17, 24, 25, 26, 27 (.bin) |
| Programa Z80 | sf2_09.bin |
| Muestras OKI MSM6295 | sf2_18.bin, sf2_19.bin |

### Movidos a la Papelera (61)

`sf2_28a/c`, `sf2_30a/c`, `sf2_31a/c`, `sf2_35a/c`, `sf2_38a/c`, `sf2e_37b.rom`,
`sf2j*.bin` (8), `sf2u.*` (30), `ssf.01`, `ssf.q01`…`ssf.q08`.

## Incidente y lección

Un comando `cd sf2 && for f in *; do mv …` corrió en la raíz del proyecto en
lugar de en `sf2/` y movió `docs/`, `recomp/`, `sf2/` y `tools/` enteras a la
Papelera durante un minuto. Se restauraron intactas. La limpieza se rehízo con
un script de Python con rutas absolutas que comprueba la cuenta de archivos
(84 en total, 23 a conservar, 61 a mover) antes de tocar nada. Regla desde hoy:
rutas absolutas siempre, y toda operación de mover o borrar pasa por un
"ensayo" que verifique cuentas y nombres.

## Pendiente

- Decidir el emulador de referencia (ver D7 en `docs/DECISIONES.md`).
- Script que genere un `sf2.zip` con los nombres de MAME actual a partir de
  estos 23 archivos, cuando toque instalar MAME (hito 2).

## Añadido más tarde el mismo día: `sf2.zip` para MAME actual

El usuario dejó `sf2.zip` (3,5 MB) en la raíz del proyecto. Comprobación con
`zipfile` de Python contra `ROM_START( sf2 )` de MAME master:

- 31 entradas, 31 de 31 archivos del set presentes, por CRC y por nombre.
- Incluye los siete PLD (`buf1`, `ioa1`, `prg1`, `rom1`, `sou1`, `stf29.1a`,
  `iob1.11d`, `c632.ic1`) que faltaban en el set antiguo.
- Los 23 archivos de contenido tienen los mismos CRC que los sueltos de `sf2/`;
  solo cambian los nombres (por ejemplo `sf2e.30g` → `sf2e_30g.11e`,
  `sf2_05.bin` → `sf2-1m.3a`, `sf2_29a.bin` → `sf2_29b.10e`).
- Ningún archivo sobrante y el test de integridad del zip pasa.

Con esto MAME actual puede arrancar el juego; ya no hace falta el script de
renombrado que se había anotado como pendiente. Para usarlo:

```bash
mame -rompath roms sf2
```

con el zip dentro de una carpeta `roms/` (o la ruta donde esté). `*.zip`,
`*.7z` y `roms/` quedan excluidos del repositorio por `.gitignore`.
