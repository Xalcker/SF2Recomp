# Nucleo portable: CPU recompilada, maquina CPS1, video, sonido, ROMs y estados guardados.
# C11 / C++17 sin llamadas al sistema (ver runtime/sf2_port.h y tests/check_core_purity.cmake).

# ---- utilidades puras: sin dependencia de la maquina -----------------------------------------------------------
add_library(sf2_base STATIC
  mem.c cps1.c state.c crc32.c zipread.c romset.c png.c)
target_include_directories(sf2_base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(sf2_base PRIVATE -Wall -Wno-unused-function)

# ---- el codigo generado --------------------------------------------------------------------------------------
add_library(sf2_recomp STATIC ${SF2_GEN_SOURCES} dispatch.c)
target_include_directories(sf2_recomp PUBLIC "${SF2_GEN_DIR}")
target_compile_definitions(sf2_recomp PUBLIC RECOMP_HOOK RECOMP_CYCLES)
target_compile_options(sf2_recomp PRIVATE -O1 -Wall -Wno-unused-variable -Wno-unused-label
  -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-value -Wno-parentheses-equality)
target_link_libraries(sf2_recomp PUBLIC sf2_base)
add_dependencies(sf2_recomp sf2_generate)

# ---- video, sonido y maquina -----------------------------------------------------------------------------------
# El sonido se compila con la contraccion a FMA por defecto del compilador (como MAME): la mezcla en float tiene
# que dar los mismos bits que la de MAME en arm64 (D31).  No se tocan -ffp-contract ni -ffast-math.
add_library(sf2_core STATIC
  gfx.c video.c sound.c z80.c oki6295.c ym2151.cpp machine.c sf2.c)
target_link_libraries(sf2_core PUBLIC sf2_recomp ymfm)
target_compile_options(sf2_core PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wno-unused-function>
                                         $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wno-unused-function>)
if(NOT MSVC AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
  find_library(SF2_LIBM m)
  if(SF2_LIBM)
    target_link_libraries(sf2_core PUBLIC "${SF2_LIBM}")
  endif()
endif()

# comprobacion del nucleo: ninguna llamada al sistema en los objetos (tests/check_core_purity.cmake)
set(SF2_CORE_OBJECT_LIBS sf2_base sf2_core PARENT_SCOPE)
