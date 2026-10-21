# Componentes de terceros (solo se compilan, nunca se descargan):
#   ymfm    (BSD-3)  sintesis del YM2151, parte del ejecutable del juego
#   musashi (MIT)    CPU 68000 interpretada: solo para los arneses de validacion y para sacar la tabla de ciclos
#                    que usa el recompilador; el juego no la lleva (D13).  Necesita ejecutar m68kmake en la
#                    maquina que compila, asi que no se define en compilaciones cruzadas.

# ---- ymfm --------------------------------------------------------------------------------------------------
if(NOT SF2_HOSTGEN)
  add_library(ymfm STATIC ymfm/src/ymfm_opm.cpp)
  target_include_directories(ymfm PUBLIC ymfm/src)
  target_compile_options(ymfm PRIVATE -w)
endif()

# ---- Musashi -----------------------------------------------------------------------------------------------
if(NOT CMAKE_CROSSCOMPILING OR SF2_HOSTGEN)
  set(MUSASHI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/musashi")
  set(MUSASHI_GEN "${CMAKE_BINARY_DIR}/musashi")
  file(MAKE_DIRECTORY "${MUSASHI_GEN}")
  add_executable(m68kmake "${MUSASHI_DIR}/m68kmake.c")
  target_compile_options(m68kmake PRIVATE -w)
  set_target_properties(m68kmake PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MUSASHI_GEN}")
  add_custom_command(OUTPUT "${MUSASHI_GEN}/m68kops.c" "${MUSASHI_GEN}/m68kops.h"
    COMMAND m68kmake "${MUSASHI_GEN}" "${MUSASHI_DIR}/m68k_in.c"
    DEPENDS m68kmake "${MUSASHI_DIR}/m68k_in.c"
    COMMENT "Generando las tablas de Musashi (m68kmake)"
    VERBATIM)
  add_library(musashi STATIC
    "${MUSASHI_DIR}/m68kcpu.c" "${MUSASHI_DIR}/m68kdasm.c" "${MUSASHI_DIR}/softfloat/softfloat.c"
    "${MUSASHI_GEN}/m68kops.c" "${MUSASHI_GEN}/m68kops.h")
  target_include_directories(musashi PUBLIC "${MUSASHI_DIR}" "${MUSASHI_GEN}")
  target_compile_options(musashi PRIVATE -w -O2 -g)
  if(UNIX AND NOT APPLE)
    target_link_libraries(musashi PUBLIC m)       # m68kfpu.c usa sin/cos (en macOS la libm va en libSystem)
  endif()
  # tabla de ciclos base por opcode (65536 bytes) para el generador de C
  add_executable(dump_cycles "${SF2_ROOT}/tools/dump_cycles.c" "${SF2_ROOT}/harness/stubs_dasm.c")
  target_link_libraries(dump_cycles PRIVATE musashi)
  target_compile_options(dump_cycles PRIVATE -w)
  set_target_properties(dump_cycles PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
