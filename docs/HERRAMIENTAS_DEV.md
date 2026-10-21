# Herramientas de desarrollo ocultas de Capcom

El bloque `0x86E98–0x8A2C8` de la ROM (2.429 instrucciones, 106 funciones) son menús de desarrollo que la placa nunca abre.
Están **recompilados** (el análisis estático los alcanza); solo faltaba una forma de entrar. La cobertura dinámica los cuenta como
«no ejecutados» (`docs/PLAN.md`, hito 6).

## Cómo los abre el juego

1. **Al encender** (`0x994`): lee `0x800000` (P2) y, si vale `0x50` (P2 puñetazo débil + fuerte), pone `0x322(a5) = 1`.
2. **Cada cuadro** (`0x1930`): con `0x322 != 0`, copia el bit 7 de DSWC invertido (el dip de pruebas) a `0x2D7(a5)`.
3. **Al entrar en el modo `0x84FBC`** (hacia el cuadro 175 tras encender): si `0x2D7 != 0`, mira `0x7F(a5)` (P1 invertido):

| P1 | Salta a | Qué es |
|---|---|---|
| puñetazo débil | `0x87ABA` | SCROLL TEST (elige SCROLL1/2/3/123 y mueve posición y velocidad) |
| puñetazo medio | `0x889A6` | OBJ TEST: editor/visor de cajas de golpe (`HEAD BODY FOOT WEAK ATCK CATCH BLOCK ATARI KAGE`, `HIT TYPE`, `ATCK DNo`, `LEVEL`…) |
| puñetazo fuerte | `0x89AB6` | validador de guiones de IA (ver abajo); **no se ofrece en el port**: sin interfaz y tarda ~30 min de juego |

Dentro de una herramienta, `0x2D7` activa además atajos de depuración (`0x87AC8`: combinación de P1 y `0x8A(a5)` que salta a `0x242E`).

## Cómo se abren en el port

`sf2_set_dev_tool(s, 0..2)` (`runtime/sf2.h`) reinicia la máquina con: P2 débil + fuerte durante 3 cuadros, el botón de P1 de la
herramienta durante 185 cuadros (el juego decide hacia el 175) y el bit 7 de DSWC a 0. Con 0 todo vuelve a ser el juego original,
idéntico a MAME. No forma parte del estado guardado.

- **Menú del juego** (M): *Pantalla ancha y mods → Herramientas de desarrollo*; izquierda/derecha elige y Enter reinicia con ella.
- **Línea de comandos**: `--dev-tool 1|2`.
- Dentro de la herramienta manda el teclado/mando de J1; F10 reinicia (vuelve a entrar en ella) y el menú → *Apagado* sale.

## Verificado

Con el arnés (`sf2_blind`) y con `build/sf2 --dev-tool N`, el cuadro 399 coincide píxel a píxel entre ambos para las dos herramientas del port. Sin la
combinación de P2 o sin el dip de pruebas el juego no entra. `make test` pasa completo.

## La herramienta 3 es un validador, no un menú

Recorre los guiones de IA de los 12 rivales en tres pasadas y solo dibuja si encuentra un error (`NO END IF!!`, `NO YON !!`,
`ILLEGAL MOSI !!`, con `ENEMY=`, `TARGET=`, `LEVEL=`, `YOKE NO=`, `RND NO=`; después se queda en un bucle en `0x8A16A`). Si todo es
correcto imprime **«OK.»** (fila 2, columna 0x10) y se queda quieto. La tercera pasada compara en un byte un contador de 16 bits
(`0x20C`), así que da 16.128 vueltas en vez de 63 (comportamiento original). Medido con `sf2_blind`: negro hasta algún cuadro entre
100.000 y 150.000 (≈ 30–40 min de juego, ~1 min de núcleo sin límite de velocidad) y «OK.» después. Con la herramienta del menú hay
que dejarla correr (Tab avanza rápido) o aceptar que se ve negra: no tiene interfaz.
