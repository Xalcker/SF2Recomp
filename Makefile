# Makefile - envoltorio de CMake (hito 5).
#
# El sistema de construccion es CMake (CMakeLists.txt en la raiz y en cada carpeta).  Este archivo conserva los
# objetivos y variables de los hitos 1 a 4 para que los mismos comandos sigan funcionando, y da el mismo resultado:
# cada objetivo configura el arbol de CMake en $(BUILD) (por omision build/) y construye el objetivo del mismo nombre.
#
#   make              -> configura y compila todo (recompilador, runtime, juego, arneses)
#   make check        -> arnes en paso cerrado 600 cuadros contra Musashi (criterio del hito 1)
#   make run-blind    -> BFRAMES cuadros a ciegas con la secuencia INPUTS; resumen en build/summary_recomp.txt
#   make compare-mame -> run-blind + el mismo resumen desde el MAME local (tools/mame_blind.lua) + comparacion
#   make check-blind  -> paso cerrado con Musashi sobre la misma secuencia de entradas
#   make run-video    -> el juego (build/sf2, SDL3) con la partida de INPUTS, en una ventana y con sonido
#   make png-video / mame-video / compare-video     -> hito 3: cuadros PNG, capturas de MAME y comparacion pixel a pixel
#   make run-audio / wav-recomp / mame-audio / compare-sound-cmds / compare-audio   -> hito 4: sonido
#   make analyze      -> informe de cobertura y desensamblado en build/
#   make dasmcheck    -> cruza el decodificador Python con el desensamblador de Musashi
#   make test         -> ctest: pruebas unitarias, de ROMs y estados guardados, regresiones y pureza del nucleo
#   make coverage     -> hito 6: todas las secuencias de tools/sequences/ a ciegas + informe de cobertura dinamica
#   make compare-coverage -> hito 6: lo mismo y cada secuencia contra MAME (resumen, sonido y video)
#   make web          -> compilacion para navegador con el Emscripten local (external/emsdk): build-web/index.html
#
# Variables: ROMDIR (sf2.zip o carpeta con las ROMs; por omision sf2), BFRAMES, FRAMES, INPUTS, VFRAMES, BUILD, CMAKE.
# Requiere clang, python3 y las ROMs; CMake se toma de external/cmake si existe y si no del sistema (ver README.md).
# compare-* requieren ademas el MAME local (external/mame/cps1, ver tools/mame.sh) y sf2.zip en la raiz.

ROMDIR  ?= sf2
BUILD   ?= build
BFRAMES ?= 12000
FRAMES  ?= 600
INPUTS  ?= tools/inputs_sf2.txt
VFRAMES ?= 100,650,900,1100,1250,1600,1800,2200,3000,3702,4400,5000,5600,6200,7000,8000,10000
LOCAL_CMAKE := external/cmake/CMake.app/Contents/bin/cmake
CMAKE   ?= $(if $(wildcard $(LOCAL_CMAKE)),$(LOCAL_CMAKE),cmake)
CTEST   ?= $(dir $(CMAKE))ctest
JOBS    ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
EMSDK_ENV := external/emsdk/emsdk_env.sh
WEBBUILD ?= build-web

CONFIGURE_ARGS = -S . -B $(BUILD) -DSF2_ROM_DIR=$(abspath $(ROMDIR)) -DSF2_BFRAMES=$(BFRAMES) -DSF2_FRAMES=$(FRAMES) \
                 -DSF2_INPUTS=$(abspath $(INPUTS)) "-DSF2_VFRAMES=$(VFRAMES)"

TARGETS := check check-blind run-blind compare-mame analyze dasmcheck png-video mame-video compare-video \
           run-audio wav-recomp mame-audio compare-sound-cmds compare-audio coverage compare-coverage

.PHONY: all configure run test web clean distclean $(TARGETS) run-video

all: configure
	$(CMAKE) --build $(BUILD) -j$(JOBS)

configure:
	$(CMAKE) $(CONFIGURE_ARGS)

# dasmcheck ya es el nombre de la herramienta en CMake; el objetivo que la ejecuta es dasmcheck-run
$(filter-out dasmcheck,$(TARGETS)): configure
	$(CMAKE) --build $(BUILD) -j$(JOBS) --target $@

dasmcheck: configure
	$(CMAKE) --build $(BUILD) -j$(JOBS) --target dasmcheck-run

run: run-blind

run-video: all
	$(BUILD)/sf2 $(ROMDIR) --inputs $(INPUTS) --frames $(BFRAMES)

test: all
	$(CTEST) --test-dir $(BUILD) --output-on-failure

# Web: el recompilador corre en la maquina anfitriona (sub-proyecto nativo) y el juego se compila con Emscripten
web:
	. $(EMSDK_ENV) >/dev/null 2>&1 && PATH="$(dir $(abspath $(CMAKE))):$$PATH" emcmake $(CMAKE) -S . -B $(WEBBUILD) \
	    -DSF2_ROM_DIR=$(abspath $(ROMDIR)) -DSF2_BUILD_TESTS=OFF && $(CMAKE) --build $(WEBBUILD) -j$(JOBS)

clean:
	-$(CMAKE) --build $(BUILD) --target clean

distclean:
	test -n "$(BUILD)" && rm -rf -- "$(BUILD)"
