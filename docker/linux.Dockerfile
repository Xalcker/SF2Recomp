# Entorno de compilacion y prueba de SF2Recomp en Linux (Debian 13 "trixie"): clang, CMake, Ninja, Python 3 y SDL3.
# No lleva el codigo ni las ROMs: tools/linux-docker.sh monta el repositorio (solo lectura) y sf2.zip al ejecutar.
FROM debian:trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential clang cmake ninja-build python3 pkg-config libsdl3-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*
ENV CC=clang CXX=clang++ PYTHONDONTWRITEBYTECODE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
WORKDIR /build
