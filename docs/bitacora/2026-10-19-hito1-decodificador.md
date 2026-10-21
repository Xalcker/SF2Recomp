# 2026-10-19 · Hito 1 · Decodificador MC68000

## Pregunta de partida

No hay Capstone ni Ghidra en la máquina (decisión D4: Python sin dependencias), así que el
recompilador necesita su propio decodificador del juego de instrucciones del 68000, suficiente
para desensamblar y para que el generador de C conozca operandos, tamaños y longitudes exactas.

## Qué se hizo

`tools/m68k.py`: decodificador completo del MC68000 (sin 68010/68020). Devuelve objetos `Insn`
(dirección, longitud, mnemónico, tamaño 1/2/4, lista de operandos, condición) y `Op` (modo de
direccionamiento, registro, desplazamiento, índice, inmediato, base PC para los modos
relativos). Puntos de diseño:

- Decodificación por líneas (4 bits altos) con validación de la clase de EA permitida en cada
  instrucción (datos, memoria, control, alterable…), de modo que los datos tomados por código se
  rechazan en cuanto aparece un opcode o un modo inválido.
- Palabra de extensión `(d8,An,Xn)`: se interpreta siempre en formato breve e ignorando el bit 8 y
  la escala, como hace el núcleo 68000 real (y el de Musashi). El desensamblador de Musashi, en
  cambio, la muestra en formato 68020; de ahí las 10 "discrepancias" de `make dasmcheck`, todas en
  zonas de datos (ver más abajo).
- `movem` normaliza la máscara de registros para que el bit i sea siempre el registro i, también en
  el modo predecremento.
- Se rechazan como inválidas las codificaciones exclusivas de 68010/020 (`bcc.l` con desplazamiento
  0xFF, `rtd`, `move from ccr`, `mull/divl`, `pack/unpk`, campos de bits, línea A/F).
- Ejecutable suelto para inspeccionar: `python3 tools/m68k.py build/rom.bin 40e 40`.

`tools/dasmcheck.c` + `make dasmcheck`: compara la longitud de cada instrucción decodificada en
Python (`build/insns.txt`) con la que calcula `m68k_disassemble` de Musashi.

## Comandos

```bash
python3 tools/m68k.py build/rom.bin 40e 40     # desensamblado lineal desde el reset
python3 tools/m68k.py build/rom.bin ab8 30     # handler de VBlank
make dasmcheck
```

## Resultados

- El arranque (0x40E) y el handler de VBlank (0xAB8) coinciden con lo decodificado a mano en el
  hito 0 y con la traza de MAME.
- `dasmcheck`: 43.216 instrucciones comparadas, 10 discrepancias de longitud. Las diez son
  palabras de extensión con el bit 8 puesto en zonas de datos alcanzadas por tablas sobreextendidas
  (0x9784, 0x21C66, 0x848EE-0x8490C); el núcleo 68000 las ejecutaría como formato breve, igual que
  nuestro decodificador, así que no son errores.
- La validación real del decodificador es el arnés en paso cerrado (ver la entrada del arnés):
  9,3 millones de instrucciones ejecutadas sin que ninguna longitud u operando difiera de Musashi.

## Pendiente

- Nada funcional. Si algún día se quiere soportar otra CPU de la familia, los puntos de extensión
  son `_ea()` (modos nuevos) y las líneas 4 y E.
