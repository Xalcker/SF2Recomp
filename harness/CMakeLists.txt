# Arneses de validacion (hitos 1 a 4): el codigo recompilado frente a Musashi paso a paso (sf2_lockstep) y la
# maquina completa "a ciegas" con resumen por cuadro, PNG y sonido (sf2_blind).  Musashi solo vive aqui (D13).
add_library(sf2_harness STATIC common.c harness.c musashi_glue.c)
target_include_directories(sf2_harness PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sf2_harness PUBLIC sf2_core sf2_hostio musashi)
target_compile_options(sf2_harness PRIVATE -Wall -Wno-unused-function)

add_executable(sf2_blind blind.c)
target_link_libraries(sf2_blind PRIVATE sf2_harness)
set_target_properties(sf2_blind PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(sf2_blind PRIVATE -Wall -Wno-unused-function)

# el paso cerrado trae su propia maquina y sus ganchos rt_*: no enlaza el nucleo, solo la CPU y la memoria
add_executable(sf2_lockstep main.c common.c musashi_glue.c)
target_link_libraries(sf2_lockstep PRIVATE sf2_recomp sf2_hostio musashi)
target_compile_options(sf2_lockstep PRIVATE -Wall -Wno-unused-function)
