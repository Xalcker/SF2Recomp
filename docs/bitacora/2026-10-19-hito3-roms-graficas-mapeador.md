# 2026-10-19 · Hito 3 · ROMs gráficas, formato de tiles y mapeador STF29

## Pregunta de partida

Cómo cargar las doce ROMs gráficas de `sf2/` para que el renderizador vea exactamente lo mismo que el
`gfx_element` de MAME, y qué código de tile corresponde a qué bytes de ROM en la placa B 90629B-3 (PAL
STF29).

## Qué se hizo

### Identificación de las ROMs

Los CRC32 de los doce archivos de 512 KiB coinciden con los de `ROM_START( sf2 )` en `cps1.cpp`
(MAME 0.289), lo que fija la correspondencia de nombres y la posición de cada uno en la región "gfx" de
6 MiB (`ROM_LOAD64_WORD`: cada ROM aporta una palabra de 16 bits cada 8 bytes, con los dos bytes en el
orden del archivo):

| sf2/ | MAME | Offset | sf2/ | MAME | Offset | sf2/ | MAME | Offset |
|---|---|---|---|---|---|---|---|---|
| sf2_06.bin | sf2-5m.4a | 0x000000 | sf2_15.bin | sf2-6m.4c | 0x200000 | sf2_25.bin | sf2-13m.4d | 0x400000 |
| sf2_08.bin | sf2-7m.6a | 0x000002 | sf2_17.bin | sf2-8m.6c | 0x200002 | sf2_27.bin | sf2-15m.6d | 0x400002 |
| sf2_05.bin | sf2-1m.3a | 0x000004 | sf2_14.bin | sf2-2m.3c | 0x200004 | sf2_24.bin | sf2-9m.3d | 0x400004 |
| sf2_07.bin | sf2-3m.5a | 0x000006 | sf2_16.bin | sf2-4m.5c | 0x200006 | sf2_26.bin | sf2-11m.5d | 0x400006 |

Cada grupo de cuatro ROMs es un banco de 2 MiB del mapeador (bancos 0, 1 y 2). `runtime/gfx.c`
(`gfx_load`) intercala los archivos en memoria al arrancar; la región nunca se escribe en disco ni entra
en el repositorio (D5).

### Formato de píxeles (gfx_layout de cps1.cpp)

Cuatro planos de un bit (`{24, 16, 8, 0}` bits de desplazamiento de plano, píxeles `STEP8(0,1)` y
`STEP8(32,1)`, filas cada 64 bits). Traducido a bytes: la unidad es un grupo de **8 bytes = 16 píxeles
de una fila**; los bytes 0-3 son los ocho píxeles de la izquierda (byte 0 = plano del bit 0 del pen,
byte 1 = bit 1, byte 2 = bit 2, byte 3 = bit 3; el bit 7 de cada byte es el píxel de más a la
izquierda) y los bytes 4-7 los ocho de la derecha. Con esa unidad:

| Tipo | Bytes por tile | Fila | Observación |
|---|---:|---|---|
| 8x8 (SCROLL1) | 64 | media unidad | las columnas pares del tilemap usan la mitad izquierda de la unidad y las impares la derecha (`cps1_layout8x8` / `cps1_layout8x8_2`, gfxset = bit 5 del índice de tile) |
| 16x16 (OBJ, SCROLL2) | 128 | una unidad | |
| 32x32 (SCROLL3) | 512 | dos unidades consecutivas | 64 unidades por tile |

`gfx_load` predecodifica toda la región a "pens" (un byte 0-15 por píxel, 12 MiB) de modo que el
renderizador solo indexa: `gfx_row8/16/32(tile, fila)` devuelven el puntero a los 8, 16 o 32 pens de una
fila. El pen 15 es transparente en todos los tipos.

### Mapeador STF29 (`gfx_map`)

Copia de `gfxrom_bank_mapper` con `mapper_STF29_table` de `cps1_v.cpp`: el código que escribe el juego
se lleva a unidades de 8x8 (desplazamiento 1 para OBJ y SCROLL2, 0 para SCROLL1, 3 para SCROLL3), se
busca en los rangos permitidos y se convierte en índice de tile dentro de la región; fuera de rango el
tile es transparente (`-1`). Resultado para SF2 (tamaños de banco 0x8000 unidades = 2 MiB):

| Tipo | Códigos válidos | Banco | Índice de tile resultante | Bytes |
|---|---|---:|---|---|
| OBJ | 0x0000-0x3FFF | 0 | = código | código × 128 |
| OBJ | 0x4000-0x7FFF | 1 | = código | código × 128 |
| OBJ | 0x8000-0x8FFF | 2 | = código | código × 128 |
| SCROLL3 | 0x0400-0x07FF | 2 | 0x2000 + código | 0x400000 + (código − 0x400) × 512 |
| SCROLL1 | 0x4000-0x4FFF | 2 | 0x10000 + código | 0x400000 + (código − 0x4000) × 64 |
| SCROLL2 | 0x2800-0x3FFF | 2 | 0x8000 + código | 0x400000 + (código − 0x2800) × 128 |

Es decir, para los sprites el mapeo es la identidad y para las tres capas de scroll todo está en el
tercer grupo de ROMs. El juego solo usa códigos dentro de estos rangos en toda la partida de referencia
(los tilemaps de las capas se rellenan con el código 0x400 de SCROLL3, 0x4020-0x46E3 de SCROLL1 y
0x2800-0x3713 de SCROLL2 en el combate).

## Comandos

```bash
python3 -I - <<'EOF'   # comprobación de CRC frente a ROM_START( sf2 )
import zlib; print('%08x' % (zlib.crc32(open('sf2/sf2_06.bin','rb').read()) & 0xffffffff))   # 22c9cc8e
EOF
make -j8                # compila runtime/gfx.c con el resto
```

Para inspeccionar un tile se usó un programa de prueba de una pantalla que llama a `gfx_load`, `gfx_map` y
`gfx_row32` e imprime los pens como caracteres (no se conserva: el tile se veía como la copa de un árbol
del escenario de Brasil).

## Resultados

- Las doce ROMs se cargan e intercalan en 0,2 s; la predecodificación ocupa 12 MiB.
- Tropiezo: la primera versión de `gfx_row32` avanzaba 32 unidades por tile en vez de 64 (512 bytes / 8),
  con lo que SCROLL3 leía filas de otros tiles. Se detectó porque la capa de árboles salía vacía y se
  corrigió antes de la primera comparación con MAME.
- Validación final: con el mapeador y la decodificación de esta entrada, los 17 cuadros de referencia
  coinciden píxel a píxel con MAME (ver la entrada de comparación).

## Pendiente

- Nada específico. El mapeador está escrito como tabla, así que otra revisión (otro PAL) sería otra
  tabla.
