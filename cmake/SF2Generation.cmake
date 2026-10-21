# SF2Generation.cmake - el recompilador como paso de la compilacion.
#
# Define SF2_GEN_DIR (carpeta con el C generado), SF2_GEN_SOURCES (los .c generados), el objetivo
# `sf2_generate` y la variable SF2_ROM_BIN (imagen de programa de 1 MiB).  Tres casos:
#   1. SF2_GENERATED_DIR dada: se usa el C que ya hay alli (por ejemplo el de un build nativo, para Web).
#   2. Compilacion nativa: se ejecuta aqui mismo (interleave.py -> dump_cycles -> gen_c.py).
#   3. Compilacion cruzada (Emscripten, mingw...): las herramientas tienen que ejecutarse en la maquina que
#      compila, asi que se lanza un sub-proyecto nativo (recompiler/host) con ExternalProject.
set(SF2_PARTS 16)
set(SF2_GEN_SOURCES "")
foreach(i RANGE 0 15)
  string(LENGTH "${i}" _len)
  if(_len EQUAL 1)
    set(_n "0${i}")
  else()
    set(_n "${i}")
  endif()
  list(APPEND SF2_GEN_SOURCES "recomp_${_n}.c")
endforeach()
list(APPEND SF2_GEN_SOURCES "recomp_table.c")

if(SF2_GENERATED_DIR)
  set(SF2_GEN_DIR "${SF2_GENERATED_DIR}")
  foreach(f ${SF2_GEN_SOURCES} recomp_funcs.h)
    if(NOT EXISTS "${SF2_GEN_DIR}/${f}")
      message(FATAL_ERROR "SF2_GENERATED_DIR=${SF2_GEN_DIR}: falta ${f}")
    endif()
  endforeach()
  add_custom_target(sf2_generate)
  list(TRANSFORM SF2_GEN_SOURCES PREPEND "${SF2_GEN_DIR}/")
  set(SF2_ROM_BIN "")
elseif(CMAKE_CROSSCOMPILING)
  if(NOT SF2_ROM_DIR)
    message(FATAL_ERROR "Indica las ROMs con -DSF2_ROM_DIR=/ruta/sf2.zip (o la carpeta con los archivos), o el C ya generado con -DSF2_GENERATED_DIR=")
  endif()
  include(ExternalProject)
  set(SF2_GEN_DIR "${CMAKE_BINARY_DIR}/hostgen/generated")
  list(TRANSFORM SF2_GEN_SOURCES PREPEND "${SF2_GEN_DIR}/" OUTPUT_VARIABLE _abs_srcs)
  set(SF2_GEN_SOURCES ${_abs_srcs})
  ExternalProject_Add(sf2_hostgen
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/recompiler/host"
    BINARY_DIR "${CMAKE_BINARY_DIR}/hostgen"
    CMAKE_ARGS -DSF2_ROM_DIR=${SF2_ROM_DIR} -DCMAKE_BUILD_TYPE=Release
    INSTALL_COMMAND ""
    # siempre se entra en el sub-build (es incremental: solo regenera si cambian las ROMs o el recompilador); con 0 una
    # compilación cruzada seguía con el C de un recompilador anterior (hito 6)
    BUILD_ALWAYS 1
    USES_TERMINAL_BUILD 1)
  add_custom_target(sf2_generate DEPENDS sf2_hostgen)
  set_source_files_properties(${_abs_srcs} "${SF2_GEN_DIR}/recomp_funcs.h" PROPERTIES GENERATED TRUE)
  set(SF2_ROM_BIN "${CMAKE_BINARY_DIR}/hostgen/rom.bin")
else()
  include(SF2Pipeline)
  sf2_generation_pipeline("${CMAKE_BINARY_DIR}")
endif()
