# SF2Recomp - recompilación estática de Street Fighter II (CPS1) a C portable.
#
#   cmake -S . -B build -DSF2_ROM_DIR=/ruta/a/sf2.zip        # o a una carpeta con las ROMs sueltas
#   cmake --build build -j8
#
# El recompilador (tools/ + recompiler/) corre como paso de la compilación sobre las ROMs que indica
# SF2_ROM_DIR y genera el C del programa del 68000 en <build>/generated; ese código deriva del programa de Capcom
# y por eso no se versiona ni se redistribuye (D5).  Ver README.md para macOS, Linux, Windows y Web.
cmake_minimum_required(VERSION 3.20)
project(SF2Recomp VERSION 0.5.0 LANGUAGES C CXX
        DESCRIPTION "Street Fighter II (CPS1): recompilacion estatica del 68000 a C portable")

set(SF2_ROOT "${CMAKE_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${SF2_ROOT}/cmake")

# ---- opciones ----------------------------------------------------------------------------------------------
set(SF2_ROM_DIR "" CACHE PATH "ROMs de SF2 (World 910522): sf2.zip de MAME o carpeta con los archivos sueltos. Necesarias para generar el codigo")
set(SF2_GENERATED_DIR "" CACHE PATH "Usar el C ya generado en esta carpeta (p. ej. <build nativo>/generated) en lugar de ejecutar el recompilador")
option(SF2_BUILD_APP "Compilar el juego (sf2, SDL3)" ON)
option(SF2_BUILD_HARNESS "Compilar los arneses de validacion con Musashi (no en Web)" ON)
option(SF2_BUILD_TESTS "Registrar las pruebas de CTest" ON)
set(SF2_BFRAMES 12000 CACHE STRING "Cuadros de la partida a ciegas (make BFRAMES=N)")
set(SF2_INPUTS "${CMAKE_SOURCE_DIR}/tools/inputs_sf2.txt" CACHE FILEPATH "Secuencia de entradas de la partida de validacion")
set(SF2_VFRAMES "100,650,900,1100,1250,1600,1800,2200,3000,3702,4400,5000,5600,6200,7000,8000,10000" CACHE STRING
    "Cuadros de video que se comparan con MAME pixel a pixel")
set(SF2_FRAMES 600 CACHE STRING "Cuadros del paso cerrado con Musashi (make check)")
set(SF2_SANITIZE "" CACHE STRING "Sanitizadores para todo el proyecto, p. ej. address,undefined (hito 6: build-asan)")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilacion" FORCE)
endif()
# Release = -O2 -g sin NDEBUG: es la configuracion con la que se valido el sonido bit a bit (D31, FMA por defecto)
if(EMSCRIPTEN)
  set(CMAKE_C_FLAGS_RELEASE "-O2")       # sin -g: con informacion de depuracion Binaryen optimiza a medias
  set(CMAKE_CXX_FLAGS_RELEASE "-O2")
else()
  set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)            # gnu11: el codigo generado usa __attribute__
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# los ejecutables van a la raiz del arbol de compilacion (build/sf2, build/sf2_blind...), como en el Makefile original
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# WebAssembly: el codigo generado necesita llamadas en cola (musttail -> tail-call de wasm) y setjmp/longjmp
# (la cesion por cuadro, ver runtime/machine.h) por excepciones nativas de wasm en lugar de las de JS
if(EMSCRIPTEN)
  add_compile_options(-mtail-call -fwasm-exceptions)
  add_link_options(-mtail-call -fwasm-exceptions)
endif()

# hito 6: AddressSanitizer y UndefinedBehaviorSanitizer en todo el árbol (núcleo, código generado, arneses y juego); un
# aviso detiene el proceso (-fno-sanitize-recover) para que ctest y tools/coverage.py lo vean como fallo
if(SF2_SANITIZE)
  add_compile_options(-fsanitize=${SF2_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${SF2_SANITIZE})
endif()

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  message(FATAL_ERROR "Compilador no soportado (${CMAKE_C_COMPILER_ID}): se necesita clang >= 13 o GCC >= 15 (musttail, ver runtime/sf2_port.h)")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- componentes -------------------------------------------------------------------------------------------
add_subdirectory(third_party)
include(SF2Generation)                 # SF2_GEN_SOURCES, SF2_GEN_DIR y el objetivo sf2_generate
add_subdirectory(runtime)
add_subdirectory(platform)
# los arneses necesitan ejecutar herramientas de Musashi en la maquina que compila: no en compilaciones cruzadas
if(SF2_BUILD_HARNESS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(harness)
  add_subdirectory(tools)
endif()
if(SF2_BUILD_APP)
  add_subdirectory(app)
endif()
if(SF2_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "SF2Recomp: ROMs='${SF2_ROM_DIR}' generado='${SF2_GEN_DIR}' tipo=${CMAKE_BUILD_TYPE} plataforma=${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
