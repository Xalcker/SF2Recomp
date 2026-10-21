#!/bin/sh
# Prueba de humo de los ejecutables de Windows (build-win/, ver README) con Wine dentro de Docker (x86-64, emulado en
# Apple silicon).  Ejecuta las pruebas unitarias, las pruebas con ROMs (carga desde zip, estados guardados) y una medida.
#   tools/windows-wine-docker.sh [carpeta de build-win] [sf2.zip]
# La traza de huellas (trace.txt) y su WAV quedan en build-wine/ (o SF2_OUT).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDWIN=${1:-$ROOT/build-win}
OUT=${SF2_OUT:-$ROOT/build-wine}
mkdir -p "$OUT"
ROMS=${2:-${SF2_ROM_DIR:-$ROOT/sf2.zip}}
docker build --platform linux/amd64 -t sf2recomp-wine -f "$ROOT/docker/wine.Dockerfile" "$ROOT/docker"
docker run --rm --platform linux/amd64 -v "$BUILDWIN":/win:ro -v "$ROOT/tools/inputs_sf2.txt":/inputs.txt:ro \
  -v "$ROMS":/roms/sf2.zip:ro -v "$OUT":/out sf2recomp-wine sh -c '
  cd /win
  wine sf2_unit_tests.exe
  wine sf2_rom_tests.exe Z:/roms/sf2.zip Z:/inputs.txt
  wine sf2.exe Z:/roms/sf2.zip --quiet --mute --config Z:/tmp/sf2.ini --inputs Z:/inputs.txt --bench 3000 --bench-mode core
  wine sf2_trace.exe Z:/roms/sf2.zip Z:/inputs.txt 12000 1000 Z:/out/trace.wav > /out/trace.txt
'
