#!/bin/sh
# Lanza el MAME mínimo (solo driver CPS1) compilado en external/mame, o el que indique SF2_MAME.
# Las ROMs se buscan en la raíz del proyecto (sf2.zip) y los datos de MAME
# (cfg, nvram, capturas, estados) van a external/mame-data/, fuera de git.
#
# Uso: tools/mame.sh [opciones de MAME...]
#   tools/mame.sh -verifyroms sf2
#   tools/mame.sh sf2                    # con ventana y sonido
#   tools/mame.sh sf2 -debug             # con el depurador gráfico de macOS
#   SF2_TRACE=/ruta/traza.log tools/mame.sh sf2 -video none -sound none \
#       -nothrottle -str 5 -debug -debugger none -autoboot_script tools/mame_trace.lua
#
# SF2_MAME: ruta del ejecutable de MAME si no está en external/mame (por ejemplo, en Windows, compilado en otro disco:
# SF2_MAME=E:/mame-sf2recomp/mame-mame0289/cps1.exe).  En Windows se ejecuta con el bash de Git o de MSYS2; CMake lo hace solo
# (SF2_MAME y SF2_BASH en la caché).
set -eu
ROOT=$(cd -P "$(dirname "$0")/.." && pwd)
MAME="${SF2_MAME:-$ROOT/external/mame/cps1}"
if [ ! -e "$MAME" ] && [ -e "$MAME.exe" ]; then
  MAME="$MAME.exe"
fi
DATA="$ROOT/external/mame-data"
if [ ! -x "$MAME" ]; then
  echo "No existe $MAME. Compílalo con: make -C external/mame SUBTARGET=cps1 SOURCES=src/mame/capcom/cps1.cpp USE_LIBSDL=1 NOWERROR=1 -j8" >&2
  echo "(o indica otro ejecutable con SF2_MAME=/ruta/a/cps1)" >&2
  exit 1
fi
mkdir -p "$DATA"
exec "$MAME" -rompath "$ROOT" -inipath "$DATA" -cfg_directory "$DATA/cfg" \
  -nvram_directory "$DATA/nvram" -snapshot_directory "$DATA/snap" \
  -state_directory "$DATA/sta" -skip_gameinfo "$@"
