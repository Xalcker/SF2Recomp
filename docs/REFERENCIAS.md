# Referencias

## Hardware CPS1

- Vídeo CPS-A/CPS-B en MAME (referencia principal de capas, sprites, prioridades):
  https://github.com/mamedev/mame/blob/master/src/mame/capcom/cps1_v.cpp
- Driver y mapa de memoria en MAME:
  https://github.com/mamedev/mame/blob/master/src/mame/capcom/cps1.cpp
- Core FPGA Arcade-CPS1 (útil para temporización y mapeo de bancos gráficos):
  https://github.com/neptuno-fpga/Arcade-CPS1
- Descripción técnica de la placa CPS1:
  https://patpend.net/technical/arcade/cps1.html

## Recompilación estática (precedentes)

- N64Recomp (MIPS → C): https://github.com/N64Recomp/N64Recomp
- XenonRecomp (PowerPC → C++): https://github.com/hedge-dev/XenonRecomp

## Componentes reutilizables

- Musashi, intérprete 68000 (MIT), solo para validación:
  https://github.com/kstenerud/Musashi
- ymfm, síntesis FM Yamaha incluido YM2151 (BSD-3), copiado en `third_party/ymfm` desde MAME 0.289:
  https://github.com/aaronsgiles/ymfm

## Sonido CPS1 (hito 4)

- Placa de sonido y mezcla en MAME 0.289 (referencia de la comparación): `src/mame/capcom/cps1.cpp`
  (`cps1_10MHz`, `sub_map`), `src/devices/sound/ymfm_mame.h` y `ymopm.cpp` (YM2151 sobre ymfm),
  `src/devices/sound/okim6295.cpp` y `okiadpcm.cpp`, `src/emu/sound.cpp` y `src/emu/resampler.cpp`
  (flujos, actualización de 50 Hz, remuestreador "lofi"), `src/emu/schedule.cpp` (orden de sucesos,
  `synchronize`, `time()` en callbacks), `src/devices/cpu/z80/z80.lst` (temporización del Z80).
- Zilog Z80 CPU User Manual (UM0080) y "The Undocumented Z80 Documented" (Sean Young) para la división en
  ciclos M y las banderas no documentadas.

## 68000

- Motorola M68000 Programmer's Reference Manual (codificación de instrucciones y banderas).

## Portabilidad (hito 5)

- CMake (binario oficial de Kitware, instalado en `external/cmake`): https://cmake.org/
- SDL3 (ventana, teclado, mandos y audio): https://wiki.libsdl.org/SDL3/ ; llamadas de aplicación por callbacks
  (`SDL_AppInit`...): https://wiki.libsdl.org/SDL3/README-main-functions
- Emscripten (instalado con `emsdk` en `external/emsdk`), puerto `--use-port=sdl3`, `-fwasm-exceptions`,
  llamadas en cola (`-mtail-call`) e IDBFS: https://emscripten.org/docs/
- llvm-mingw (clang + lld + UCRT para Windows, en `external/llvm-mingw`): https://github.com/mstorsjo/llvm-mingw ;
  bibliotecas de SDL3 para mingw: `SDL3-devel-*-mingw.tar.gz` de https://github.com/libsdl-org/SDL/releases
- Atributo `musttail` de clang: https://clang.llvm.org/docs/AttributeReference.html#musttail
- RFC 1951 (DEFLATE) y la especificación del formato zip (APPNOTE.TXT de PKWARE), para `runtime/zipread.c`.
- Dipswitches de SF2 (`INPUT_PORTS_START( sf2 )`, `CPS1_COINAGE_1`, `CPS1_DIFFICULTY_1`): `src/mame/capcom/cps1.cpp` de MAME.
