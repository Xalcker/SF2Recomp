# 2026-10-19 · Hito 1 · Intercalado de ROMs para la cadena de compilación

## Pregunta de partida

El hito 0 ya intercalaba el programa con `tools/probe_rom.py` para inspeccionarlo. Para el
recompilador hacía falta una herramienta de construcción que produzca la imagen plana de forma
reproducible desde el Makefile, comprobando tamaños y mostrando los vectores.

## Qué se hizo

`tools/interleave.py`: intercala los cuatro pares de ROMs de programa (bytes pares = byte alto
de cada palabra, el 68000 es big-endian) en una imagen de 0x100000 bytes y escribe el SHA-1 y
los vectores de reset e IRQ2.

| Rango 68000     | Bytes pares | Bytes impares |
|-----------------|-------------|---------------|
| 0x00000-0x3FFFF | sf2e.30g    | sf2e.37g      |
| 0x40000-0x7FFFF | sf2e.31g    | sf2e.38g      |
| 0x80000-0xBFFFF | sf2e.28g    | sf2e.35g      |
| 0xC0000-0xFFFFF | sf2_29a.bin | sf2_36a.bin   |

La primera versión del día intercalaba solo tres pares (0xC0000 bytes), siguiendo el contexto
inicial del hito. Al llegar la corrección del análisis del hito 0 (cuarto par según
`ROM_START( sf2 )` de MAME) se actualizaron `interleave.py`, `ROM_END` en `tools/analyze.py` y
`ROM_SIZE` en `runtime/mem.h`. El cuarto segmento no contiene código (ni `rts` ni destinos de
salto), pero el juego **sí lee datos de él**: con la imagen de 768 KB el arnés contaba 2048
lecturas "sin mapear" en 600 cuadros, todas de una copia en bloque en 0x1D20 hacia la RAM
gráfica; con 1 MB son 0.

## Comandos

```bash
python3 tools/interleave.py sf2 build/rom.bin
# o, desde el Makefile:
make build/rom.bin
```

## Resultados

```
escrito build/rom.bin: 0x100000 bytes, sha1=6bb03cf7a2b39d39fb3e9656f8bcd3a5915524f0
vector reset: SSP=0x00000000 PC=0x0000040e  IRQ2(autovector 0x68)=0x00000ab8
```

El SHA-1 coincide con el de `probe_rom.py` del hito 0. La imagen vive en `build/`, excluida del
repositorio (`build/`, `*.bin` en `.gitignore`, decisión D5).

## Pendiente

- Nada en esta pieza. `probe_rom.py` queda como herramienta de inspección y `interleave.py` como
  paso de construcción; ambas comparten la tabla de pares.
