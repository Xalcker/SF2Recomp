# 2026-10-19 · Hito 0 · Análisis de la ROM

## Pregunta de partida

¿Es viable una recompilación estática del juego? Primero había que saber qué
juego y qué revisión hay en `sf2/`, y cómo está estructurado el programa.

## Qué hay en `sf2/`

Es el set de ROMs de MAME de **Street Fighter II: The World Warrior** (CPS1),
no de Street Fighter 1. Contiene:

- Programa 68000 de varias revisiones: `sf2e.*g` más `sf2_29a.bin` y
  `sf2_36a.bin` (World 910522, la que usamos), `sf2u.*` (USA, varias letras),
  `sf2j*` (Japón), `sf2_28a`…`sf2_38c` (otras revisiones World y USA).
- `sf2_09.bin`: programa del Z80 de sonido (64 KB).
- `sf2_18.bin`, `sf2_19.bin`: muestras ADPCM del OKI MSM6295.
- `sf2_05`…`sf2_27.bin` (512 KB cada uno): ROMs gráficas, 6 MB en total.
- `ssf.01`, `ssf.q01`…`ssf.q08`: sonido de Super Street Fighter II (CPS2).
  No pertenecen a este juego y no se usan.

## Cómo se forma el programa 68000

El bus es de 16 bits y cada par de ROMs guarda bytes pares e impares:

| Rango        | Bytes pares | Bytes impares |
|--------------|-------------|---------------|
| 0x00000–0x3FFFF | sf2e.30g | sf2e.37g |
| 0x40000–0x7FFFF | sf2e.31g | sf2e.38g |
| 0x80000–0xBFFFF | sf2e.28g | sf2e.35g |
| 0xC0000–0xFFFFF | sf2_29a.bin | sf2_36a.bin |

Total: 0x100000 bytes (1 MB). **Corrección del mismo día:** el primer análisis
solo intercaló tres pares (768 KB); al cotejar los CRC con `ROM_START( sf2 )` de
MAME apareció el cuarto par, que en MAME actual se llama `sf2_29b.10e` y
`sf2_36b.10f`. Ese último segmento no contiene ningún `rts` ni recibe saltos
absolutos: parece ser solo datos. Comando usado:

```bash
python3 -I tools/probe_rom.py sf2 /tmp/sf2_prog.bin
```

## Resultados

| Dato | Valor |
|---|---|
| SSP inicial (vector 0) | 0x00000000 (el código fija la pila por su cuenta) |
| Reset (vector 1) | 0x0000040E |
| IRQ2 (VBlank) | 0x00000AB8 |
| IRQ1, IRQ3–IRQ7 | 0x0000040C, que es un `rte` |
| `jsr`/`jmp` absolutos a RAM (0xFFxxxx) | 0 (no hay código en RAM) |
| `jmp (d8,pc,xn)` (tablas de salto) | ~700 |
| `jsr (d8,pc,xn)` | ~100 |
| `jmp (an)` / `jsr (an)` | ~44 / ~22 |
| `rts` | ~1976 |
| Bytes 0xFF (relleno) | 1 a 9 % según segmento |
| SHA-1 de la imagen intercalada (1 MB) | 6bb03cf7a2b39d39fb3e9656f8bcd3a5915524f0 |

Los conteos vienen de un barrido lineal, así que los datos pueden colarse como
falsos positivos. Sirven como orden de magnitud, no como cifra exacta.

## Observaciones sobre el código

- El reset escribe primero 0x80 y luego 0x00 en 0x800030 (control de monedas y
  bloqueo), el patrón habitual de arranque CPS1.
- El handler de VBlank guarda todos los registros, toma A5 = 0xFFFF8000 como
  base de la RAM de trabajo y copia registros "sombra" de RAM a los registros
  del CPS-A (0x800100, 0x800108). También lee 0x800172 (CPS-B).
- Conclusión: el juego está dirigido por cuadro. La lógica corre entre
  interrupciones y la interrupción vuelca el estado al hardware. Es el caso
  favorable para la recompilación estática.
- El gran número de tablas de salto PC-relativas es el principal trabajo del
  desensamblador: hay que reconocer el idioma
  `add.w d0,d0 / move.w t(pc,d0.w),d0 / jmp t(pc,d0.w)` y enumerar sus destinos.

## Herramientas en la máquina

Disponibles: clang, gcc, python3, SDL2 y SDL3 (Homebrew).
Faltan: MAME (trazas de referencia), Capstone (desensamblador), Ghidra.

## Pendiente

- Iniciar repositorio git con `.gitignore` que excluya `sf2/` (añadido hoy).
- Hito 1: recompilador y arnés de validación (en curso en una sesión aparte).
- Ver la entrada siguiente: identificación del set por CRC y limpieza de ROMs.
