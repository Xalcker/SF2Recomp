# 2026-10-19 · Hito 1 · Generador de C y runtime de CPU

## Pregunta de partida

Cómo convertir las 1.364 funciones del análisis en C que compile rápido, ejecute exactamente
igual que el 68000 y sobreviva a los retornos "no estructurados" del núcleo multitarea del juego.

## Qué se hizo

### `recompiler/gen_c.py`

- Una función C por función 68000: `void f_XXXXXX(M68K *c)`. Empieza con `switch (c->pc)` sobre
  todas sus etiquetas (inicios de bloque despachables), lo que permite entrar a mitad de función.
- `jsr X`: `push32(ret); c->pc = X; f_X(c); if (c->pc != ret) return;`. `rts`: `c->pc = pop32();
  return;`. Si el valor sacado no es el esperado, cada nivel C retorna hasta que uno coincida o se
  llegue al trampolín de `main.c`, que busca `c->pc` en la tabla de despacho (D9).
- `jmp X` dentro de la función: `goto`; a otra función: llamada en cola garantizada con
  `__attribute__((musttail))` (la pila C no crece con los `lea ret(pc),a4 ; bra sub` del arranque);
  `jmp (ea)` con destinos conocidos: `switch` con `goto`, y por defecto despacho por tabla.
- Excepciones (`trap`, `chk`, división por cero, `trapv`, `illegal`, violación de privilegio):
  apilan SR y PC como el 68000, ejecutan el manejador recompilado anidado y comprueban el retorno.
- Cada instrucción emite `INSN(dir); CYC(n);` (PC actual, gancho del arnés y ciclos base de la
  tabla de Musashi, volcada por `tools/dump_cycles.c`) y un bloque `{ … }` con el cálculo de EA,
  la lectura, la operación con banderas y la escritura. Los efectos `(An)+` / `-(An)` siguen el
  orden del 68000 (fuente antes que destino) y la regla de A7 con bytes.
- Tabla de despacho `recomp_table.c`: cada etiqueta -> función propietaria (D11). Salida en
  `generated/` (16 archivos para compilar en paralelo), excluida del repositorio (D5).

### `runtime/cpu.h` y `runtime/mem.c`

- `struct M68K`: `d[8]`, `a[8]`, `pc`, `sp_other` (pila inactiva USP/SSP), banderas X N Z V C como
  bytes 0/1, bits S y T, máscara de IRQ, contadores. `set_sr` intercambia A7 con `sp_other` al
  cambiar S; `exception()` y `exception_interrupt()` apilan el marco de 3 palabras del 68000.
- Banderas calculadas igual que Musashi, incluidos los comportamientos no documentados que un
  arnés instrucción a instrucción sí detecta: V y N de ABCD/SBCD/NBCD, V de ASL, DIV con
  desbordamiento (solo V), CHK (Z de la fuente), cuentas de desplazamiento ≥ tamaño, ROX módulo
  9/17/33, CMPA/ADDA/SUBA con extensión de signo, MOVEM con el valor inicial del registro (D8).
- Memoria CPS1: ROM 0x000000-0x0FFFFF, E/S 0x800000-0x8001FF, RAM gráfica 0x900000-0x92FFFF, RAM
  de trabajo 0xFF0000-0xFFFFFF; accesos de 32 bits partidos en dos de 16 como el bus real; E/S como
  stubs (entradas sin pulsar, dipswitches por defecto, ID del CPS-B-11 = 0x0401 en 0x800172, el
  resto almacena) con contador y log de escrituras por instrucción para el arnés.

## Comandos

```bash
make generated/.stamp     # solo generar (python3 recompiler/gen_c.py build/rom.bin generated …)
make -j8                  # generar + compilar runtime, código generado y arneses
```

## Resultados

- 161.113 líneas de C (42 MB) en 16 archivos; compilan con clang `-O1` en ~11 s de reloj con
  `-j8` (~40 s de CPU). 43.216 instrucciones, 17.895 duplicadas entre funciones por bloques
  compartidos.
- Errores de generación encontrados y corregidos el mismo día: etiquetas `goto` sin declarar por
  flujos de código solapados (resuelto deteniendo los barridos solapados en el análisis y
  declarando etiqueta para todo destino de `goto` del cuerpo).
- La corrección del código generado la mide el arnés (entrada siguiente): 600 y 7.200 cuadros sin
  divergencia.

## Pendiente

- Rendimiento (hito 6): hoy prima la corrección; `INSN()` llama a un gancho por instrucción y las
  banderas se calculan siempre. Sin gancho, el binario autónomo corre 600 cuadros en ~0,05 s.
- Reducir la duplicación de bloques compartidos (funciones "epílogo" comunes) si el tamaño del C
  generado se vuelve un problema.
