# 2026-10-19 · Hito 0 · MAME compilado dentro del proyecto

## Pregunta de partida

¿Se puede tener MAME solo en este proyecto, sin instalarlo en el sistema?
Sí: MAME no publica binarios para macOS (la release 0.289 solo trae
ejecutables Windows y el código fuente), así que se compila una versión
mínima con el driver CPS1 dentro de `external/`.

## Qué se hizo

```bash
git clone --depth 1 --branch mame0289 https://github.com/mamedev/mame.git external/mame
make -C external/mame SUBTARGET=cps1 SOURCES=src/mame/capcom/cps1.cpp USE_LIBSDL=1 NOWERROR=1 -j8
```

- `SUBTARGET=cps1 SOURCES=...`: compila solo el driver CPS1 y los dispositivos
  de los que depende (68000, Z80, YM2151, OKI MSM6295, etc.). 233 máquinas.
- `USE_LIBSDL=1`: usa el SDL2 de Homebrew (aquí `sdl2-compat` 2.32) en vez del
  framework SDL2 en `/Library/Frameworks`.
- `NOWERROR=1`: evita que un aviso del compilador rompa la compilación.

| Dato | Valor |
|---|---|
| Versión | MAME 0.289 (2026-07-30) |
| Máquina | Apple M1 Pro, 8 núcleos, Xcode clang |
| Tiempo | unos 6 minutos (clon 1 min, compilación 5 min) |
| Espacio | 1,6 GB en `external/mame` (fuentes 0,7 GB + objetos) |
| Binario | `external/mame/cps1`, 83 MB (no `mamecps1`, como se esperaba) |

`external/` está en `.gitignore`.

## Herramientas añadidas

- `tools/mame.sh`: lanza el binario con `-rompath` en la raíz del proyecto
  (donde está `sf2.zip`) y con cfg, nvram, capturas y estados en
  `external/mame-data/`, para no ensuciar el proyecto ni el home.
- `tools/mame_trace.lua`: activa una traza del 68000 desde Lua y deja correr.

## Pruebas

```bash
tools/mame.sh -verifyroms sf2
# romset sf2 is good

SF2_TRACE=/tmp/traza.log tools/mame.sh sf2 -video none -sound none -nothrottle \
    -str 1 -debug -debugger none -autoboot_script tools/mame_trace.lua
```

- Verificación del set: correcta (31 archivos, con los PLD).
- Ejecución sin ventana de 1 segundo emulado: sale con código 0 y guarda la
  captura final en `external/mame-data/snap/sf2/0000.png`: la pantalla de test
  de RAM del CPS1 (SCR 1/2/3 RAM OK, OBJECT).
- Traza: 543 líneas con detección de bucles. Las primeras instrucciones son
  exactamente las decodificadas a mano en el análisis del hito 0: escritura
  de 0x80 y 0x00 en 0x800030, `lea`, `bra $73c` y el bucle que limpia la RAM
  gráfica desde 0x900000.

## Hallazgo: `-debugscript` no sirve sin ventana

Con `-debugger none`, el módulo reanuda la CPU en cuanto se detiene
(`wait_for_debugger` llama a `go()`), y `process_source_file()` solo lee el
archivo mientras la CPU está detenida (`src/emu/debug/debugcpu.cpp`, bucle de
espera). Resultado: el archivo de `-debugscript` nunca se ejecuta. La
alternativa que funciona es un script Lua con `-autoboot_script`, que llama a
`manager.machine.debugger:command("trace ...")`. Nota: `emu.register_start`
ya no existe en 0.289; el script de autoarranque corre con la máquina ya en
marcha, así que puede actuar directamente.

## Pendiente

- Para el hito 2: script Lua que vuelque registros por cuadro (o por
  instrucción con formato fijo) para comparar con el código recompilado, y
  capturas periódicas para el hito 3.
- Si hace falta recompilar MAME, repetir el `make` anterior; `tools/mame.sh`
  avisa si el binario no existe.
