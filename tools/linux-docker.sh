#!/bin/sh
# Compila y prueba SF2Recomp en Linux dentro de Docker, sin instalar nada en la maquina anfitriona.
#
#   tools/linux-docker.sh                       # arm64 (nativo en Apple silicon)
#   PLATFORM=linux/amd64 tools/linux-docker.sh  # x86-64 (emulado en Apple silicon: mucho mas lento)
#   SF2_ROM_DIR=/ruta/sf2.zip tools/linux-docker.sh
#   tools/linux-docker.sh shell                 # abre una consola en el contenedor
#
# El repositorio se monta de solo lectura en /src, las ROMs en /roms/sf2.zip (o la carpeta en /roms) y el arbol
# de compilacion vive en un volumen de Docker (sf2recomp-build-<arquitectura>), no en la carpeta del proyecto.
# Salida: lo que imprime ctest y las medidas; el resumen de 12.000 cuadros, el WAV y la traza de huellas (trace.txt,
# trace.wav) quedan en build-linux-<arquitectura>/ (o SF2_OUT) para compararlos con los de macOS.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLATFORM=${PLATFORM:-linux/arm64}
ROMS=${SF2_ROM_DIR:-$ROOT/sf2.zip}
ARCH=${PLATFORM#linux/}
VOLUME=sf2recomp-build-$ARCH
IMAGE=sf2recomp-linux
OUT=${SF2_OUT:-$ROOT/build-linux-$ARCH}       # aqui quedan el resumen y el WAV de 12.000 cuadros
mkdir -p "$OUT"

docker build --platform "$PLATFORM" -t "$IMAGE" -f "$ROOT/docker/linux.Dockerfile" "$ROOT/docker"

if [ -d "$ROMS" ]; then ROMMOUNT="-v $ROMS:/roms:ro"; ROMARG=/roms; else ROMMOUNT="-v $ROMS:/roms/sf2.zip:ro"; ROMARG=/roms/sf2.zip; fi

if [ "${1:-}" = shell ]; then
  exec docker run --rm -it --platform "$PLATFORM" -v "$ROOT":/src:ro $ROMMOUNT -v "$VOLUME":/build "$IMAGE" bash
fi

docker run --rm --platform "$PLATFORM" -v "$ROOT":/src:ro $ROMMOUNT -v "$VOLUME":/build -v "$OUT":/out "$IMAGE" sh -c "
  set -e
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSF2_ROM_DIR=$ROMARG
  cmake --build /build
  ctest --test-dir /build --output-on-failure
  echo '--- huellas para comparar con macOS (12000 cuadros)'
  /build/sf2_blind /build/rom.bin --frames 12000 --inputs /src/tools/inputs_sf2.txt --summary /build/summary_linux.txt \
      --sound-roms $ROMARG --wav /build/sf2_linux.wav --quiet > /dev/null
  /build/sf2_trace $ROMARG /src/tools/inputs_sf2.txt 12000 1000 /out/trace.wav > /out/trace.txt
  cp /build/summary_linux.txt /build/sf2_linux.wav /out/
  sha256sum /build/summary_linux.txt /build/sf2_linux.wav
  /build/sf2 $ROMARG --quiet --mute --inputs /src/tools/inputs_sf2.txt --bench 12000 --bench-mode core
  /build/sf2 $ROMARG --quiet --mute --inputs /src/tools/inputs_sf2.txt --bench 12000 --bench-mode video
  /build/sf2 $ROMARG --quiet --inputs /src/tools/inputs_sf2.txt --bench 6000 --bench-mode full
"
