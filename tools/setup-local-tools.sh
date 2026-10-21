#!/bin/sh
# Instala dentro del proyecto (en external/) las herramientas de construccion que no vienen con el sistema, sin tocar
# nada fuera de la carpeta del proyecto:
#
#   tools/setup-local-tools.sh cmake      CMake (binario oficial de Kitware para macOS)
#   tools/setup-local-tools.sh emsdk      Emscripten (emsdk) para compilar para el navegador
#   tools/setup-local-tools.sh mingw      llvm-mingw + bibliotecas de SDL3 para mingw (cruzado a Windows)
#   tools/setup-local-tools.sh all
#
# Cada descarga se verifica con un SHA-256 fijado aqui (el que publica el proyecto de origen en la version indicada)
# y se extrae en una carpeta nueva; nada de lo descargado se ejecuta antes de verificarlo.  Versiones con las que se
# ha probado el proyecto (2026-10-19): CMake 4.4.4, Emscripten 6.0.13, llvm-mingw 20261006 (clang 23.1.3), SDL3 3.4.18.
# En Linux y Windows se usan los paquetes de la distribucion (ver README.md); este guion es para macOS.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EXT=${SF2_EXTERNAL:-$ROOT/external}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/sf2-setup.XXXXXX")
trap 'rm -rf "${TMP:?}"' EXIT

CMAKE_VERSION=4.4.4
CMAKE_SHA256=4b7b73704b1db9b374e5c9ab8e17ac6148b817b6396ee75cd94a852cbac9d305
EMSDK_VERSION=6.0.13
MINGW_VERSION=20261006
MINGW_FILE=llvm-mingw-$MINGW_VERSION-ucrt-macos-universal.tar.xz
MINGW_SHA256=a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6
SDL_VERSION=3.4.18
SDL_MINGW_FILE=SDL3-devel-$SDL_VERSION-mingw.tar.gz
SDL_MINGW_SHA256=049b9dd711e3fb3f8d84feb998dfec54a75e17cf539c4a80ff4d058d839771cf

fetch() {   # url destino sha256
  curl -fL --progress-bar -o "$2" "$1"
  got=$(shasum -a 256 "$2" | awk '{print $1}')
  if [ "$got" != "$3" ]; then
    echo "SHA-256 NO COINCIDE para $1: esperado $3, obtenido $got" >&2
    exit 1
  fi
}

install_cmake() {
  [ -e "$EXT/cmake" ] && { echo "external/cmake ya existe; no se toca"; return; }
  f=cmake-$CMAKE_VERSION-macos-universal.tar.gz
  fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/$f" "$TMP/$f" "$CMAKE_SHA256"
  mkdir -p "$EXT/cmake"
  tar -xzf "$TMP/$f" -C "$EXT/cmake" --strip-components=1
  "$EXT/cmake/CMake.app/Contents/bin/cmake" --version | head -1
}

install_emsdk() {
  [ -e "$EXT/emsdk" ] && { echo "external/emsdk ya existe; no se toca"; return; }
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EXT/emsdk"
  "$EXT/emsdk/emsdk" install "$EMSDK_VERSION"        # descarga LLVM, Binaryen y Node de los repositorios de emsdk (~1,8 GB)
  "$EXT/emsdk/emsdk" activate "$EMSDK_VERSION"
}

install_mingw() {
  if [ ! -e "$EXT/llvm-mingw" ]; then
    fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$MINGW_VERSION/$MINGW_FILE" "$TMP/$MINGW_FILE" "$MINGW_SHA256"
    mkdir -p "$EXT/llvm-mingw"
    tar -xJf "$TMP/$MINGW_FILE" -C "$EXT/llvm-mingw" --strip-components=1
  else
    echo "external/llvm-mingw ya existe; no se toca"
  fi
  if [ ! -e "$EXT/sdl3-mingw" ]; then
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/$SDL_MINGW_FILE" "$TMP/$SDL_MINGW_FILE" "$SDL_MINGW_SHA256"
    mkdir -p "$EXT/sdl3-mingw"
    tar -xzf "$TMP/$SDL_MINGW_FILE" -C "$EXT/sdl3-mingw" --strip-components=1
  else
    echo "external/sdl3-mingw ya existe; no se toca"
  fi
}

mkdir -p "$EXT"
case "${1:-all}" in
  cmake) install_cmake ;;
  emsdk) install_emsdk ;;
  mingw) install_mingw ;;
  all) install_cmake; install_emsdk; install_mingw ;;
  *) echo "uso: $0 cmake|emsdk|mingw|all" >&2; exit 2 ;;
esac
echo "listo"
