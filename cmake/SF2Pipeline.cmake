# SF2Pipeline.cmake - interleave.py -> dump_cycles -> gen_c.py, como reglas de compilacion de CMake.
#   sf2_generation_pipeline(<carpeta de salida>)
# Deja SF2_GEN_DIR, SF2_GEN_SOURCES (rutas absolutas), SF2_ROM_BIN y el objetivo sf2_generate en el ambito del
# llamador.  Necesita el objetivo `dump_cycles` (tabla de ciclos de Musashi) y Python 3.
function(sf2_generation_pipeline OUTDIR)
  if(NOT SF2_ROM_DIR)
    message(FATAL_ERROR
      "Faltan las ROMs: indica el zip de MAME o la carpeta con los archivos con\n"
      "  cmake -S . -B build -DSF2_ROM_DIR=/ruta/a/sf2.zip\n"
      "(el recompilador genera el C a partir de ellas; no se incluyen en el repositorio). Si ya tienes el C generado:\n"
      "  -DSF2_GENERATED_DIR=/ruta/a/<otro build>/generated")
  endif()
  set(tools "${SF2_ROOT}/tools")
  set(gen "${OUTDIR}/generated")
  set(rom_bin "${OUTDIR}/rom.bin")
  set(cycles_bin "${OUTDIR}/cycles68000.bin")

  # dependencias de la imagen: el zip, o los archivos de la carpeta
  if(IS_DIRECTORY "${SF2_ROM_DIR}")
    file(GLOB rom_files CONFIGURE_DEPENDS "${SF2_ROM_DIR}/*")
  else()
    set(rom_files "${SF2_ROM_DIR}")
  endif()
  add_custom_command(OUTPUT "${rom_bin}"
    COMMAND "${Python3_EXECUTABLE}" "${tools}/interleave.py" "${SF2_ROM_DIR}" "${rom_bin}"
    DEPENDS "${tools}/interleave.py" ${rom_files}
    COMMENT "Intercalando las ROMs de programa (${SF2_ROM_DIR})"
    VERBATIM)
  add_custom_command(OUTPUT "${cycles_bin}"
    COMMAND dump_cycles "${cycles_bin}"
    DEPENDS dump_cycles
    COMMENT "Tabla de ciclos del 68000 de Musashi"
    VERBATIM)

  set(srcs "")
  foreach(f ${SF2_GEN_SOURCES})
    list(APPEND srcs "${gen}/${f}")
  endforeach()
  add_custom_command(OUTPUT ${srcs} "${gen}/recomp_funcs.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${gen}"
    COMMAND "${Python3_EXECUTABLE}" "${SF2_ROOT}/recompiler/gen_c.py" "${rom_bin}" "${gen}"
            --hints "${tools}/hints.txt" --mods "${SF2_ROOT}/recompiler/mod_patches.txt" --cycles "${cycles_bin}" --parts ${SF2_PARTS}
            --report "${OUTDIR}/coverage.txt"
    DEPENDS "${SF2_ROOT}/recompiler/gen_c.py" "${SF2_ROOT}/recompiler/m68000_ack.txt" "${tools}/m68k.py" "${tools}/analyze.py" "${tools}/hints.txt" "${SF2_ROOT}/recompiler/mod_patches.txt"
            "${rom_bin}" "${cycles_bin}"
    COMMENT "Recompilando el 68000 a C (gen_c.py, 16 partes)"
    VERBATIM)
  add_custom_target(sf2_generate DEPENDS ${srcs} "${gen}/recomp_funcs.h")

  set(SF2_GEN_DIR "${gen}" PARENT_SCOPE)
  set(SF2_GEN_SOURCES "${srcs}" PARENT_SCOPE)
  set(SF2_ROM_BIN "${rom_bin}" PARENT_SCOPE)
  set(SF2_CYCLES_BIN "${cycles_bin}" PARENT_SCOPE)
endfunction()
