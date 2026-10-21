# Wine para probar los .exe de Windows compilados de forma cruzada (llvm-mingw) sin una maquina Windows.
# Solo para pruebas de humo: Wine no es Windows.  tools/windows-wine-docker.sh monta build-win/ y las ROMs.
FROM --platform=linux/amd64 debian:trixie-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends wine ca-certificates \
 && rm -rf /var/lib/apt/lists/*
ENV WINEDEBUG=-all WINEPREFIX=/tmp/wineprefix SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
