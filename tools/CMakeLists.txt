# Herramientas de analisis y objetivos de validacion contra MAME (los mismos que tenia el Makefile del hito 4).
add_executable(render_dump render_dump.c)
target_link_libraries(render_dump PRIVATE sf2_core sf2_hostio)
set_target_properties(render_dump PROPERTIES LINKER_LANGUAGE CXX)
add_executable(z80dis z80dis.c)
target_link_libraries(z80dis PRIVATE sf2_core)
set_target_properties(z80dis PROPERTIES LINKER_LANGUAGE CXX)
add_executable(dasmcheck dasmcheck.c "${SF2_ROOT}/harness/stubs_dasm.c")
target_link_libraries(dasmcheck PRIVATE musashi)
target_compile_options(dasmcheck PRIVATE -w)

set(B "${CMAKE_BINARY_DIR}")
set(T "${SF2_ROOT}/tools")
set(ROMSRC "${SF2_ROM_DIR}")
set(BFR ${SF2_BFRAMES})
set(INP "${SF2_INPUTS}")
math(EXPR MAME_STR "${BFR} / 55 + 15")
# MAME sin ventana ni sonido y SIN dispositivos de entrada del anfitrion (D20)
set(MAME_HEADLESS -video none -sound none -nothrottle -keyboardprovider none -mouseprovider none
                  -joystickprovider none -lightgunprovider none)
set(MAME "${T}/mame.sh")

# ---- analisis -----------------------------------------------------------------------------------------------
add_custom_target(analyze
  COMMAND "${Python3_EXECUTABLE}" "${T}/analyze.py" "${SF2_ROM_BIN}" --hints "${T}/hints.txt"
          --report "${B}/coverage.txt" --disasm "${B}/disasm.txt" --insns "${B}/insns.txt"
  DEPENDS sf2_generate USES_TERMINAL)
# dasmcheck informa 10 discrepancias de longitud cosmeticas (Musashi desensambla la extension (d8,An,Xn) con el bit 8 en
# formato 68020; el nucleo 68000 la ignora, ver la bitacora del hito 1) y sale con 1: como en el Makefile original, el
# resultado se muestra pero no hace fallar el objetivo
add_custom_target(dasmcheck-run
  COMMAND "${Python3_EXECUTABLE}" "${T}/analyze.py" "${SF2_ROM_BIN}" --hints "${T}/hints.txt"
          --report "${B}/coverage.txt" --disasm "${B}/disasm.txt" --insns "${B}/insns.txt"
  COMMAND "${CMAKE_COMMAND}" "-DCMD=$<TARGET_FILE:dasmcheck>;${SF2_ROM_BIN};${B}/insns.txt" -P "${T}/run_ignore_status.cmake"
  DEPENDS dasmcheck sf2_generate USES_TERMINAL VERBATIM)

# ---- hito 1: paso cerrado con Musashi -----------------------------------------------------------------------
add_custom_target(check COMMAND sf2_lockstep "${SF2_ROM_BIN}" --frames ${SF2_FRAMES} DEPENDS sf2_lockstep USES_TERMINAL)
add_custom_target(check-blind COMMAND sf2_lockstep "${SF2_ROM_BIN}" --irq-frames ${BFR} --inputs "${INP}"
                  --summary "${B}/summary_lockstep.txt" --quiet DEPENDS sf2_lockstep USES_TERMINAL)

# ---- hito 2: maquina a ciegas y comparacion con MAME --------------------------------------------------------
add_custom_target(run-blind
  COMMAND sf2_blind "${SF2_ROM_BIN}" --frames ${BFR} --inputs "${INP}" --summary "${B}/summary_recomp.txt"
          --io-table "${B}/io_table.md"
  DEPENDS sf2_blind USES_TERMINAL)
add_custom_command(OUTPUT "${B}/summary_mame.txt"
  COMMAND "${CMAKE_COMMAND}" -E env SF2_SUMMARY=${B}/summary_mame.txt SF2_INPUTS=${INP} SF2_FRAMES=${BFR}
          "${MAME}" sf2 ${MAME_HEADLESS} -str ${MAME_STR} -autoboot_script "${T}/mame_blind.lua"
  DEPENDS "${T}/mame_blind.lua" "${INP}" VERBATIM USES_TERMINAL)
add_custom_target(compare-mame
  COMMAND "${Python3_EXECUTABLE}" "${T}/compare_summaries.py" "${B}/summary_recomp.txt" "${B}/summary_mame.txt" --inputs "${INP}"
  DEPENDS run-blind "${B}/summary_mame.txt" USES_TERMINAL)

# ---- hito 3: video --------------------------------------------------------------------------------------------
string(REPLACE "," ";" VFRAME_LIST "${SF2_VFRAMES}")
list(SORT VFRAME_LIST COMPARE NATURAL)
list(GET VFRAME_LIST -1 VFRAMES_LAST)
math(EXPR VFRAMES_N "${VFRAMES_LAST} + 1")
math(EXPR VMAME_STR "${VFRAMES_N} / 55 + 15")
set(VDIR "${B}/video")
file(MAKE_DIRECTORY "${VDIR}")
# la lista de cuadros se guarda en un archivo que solo cambia de fecha cuando cambia la lista
file(CONFIGURE OUTPUT "${VDIR}/vframes.txt" CONTENT "${SF2_VFRAMES}\n" @ONLY)
add_custom_target(png-video
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${VDIR}"
  COMMAND sf2_blind "${SF2_ROM_BIN}" --frames ${VFRAMES_N} --inputs "${INP}" --gfx "${ROMSRC}"
          --png-frames ${SF2_VFRAMES} --png-dir "${VDIR}" --quiet
  DEPENDS sf2_blind USES_TERMINAL)
add_custom_command(OUTPUT "${VDIR}/mame/.stamp"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${VDIR}/mame"
  COMMAND "${CMAKE_COMMAND}" -E env SF2_SNAP_FRAMES=${SF2_VFRAMES} SF2_INPUTS=${INP} SF2_FRAMES=${VFRAMES_N}
          SF2_DUMP_DIR=${VDIR}/mame "${MAME}" sf2 ${MAME_HEADLESS} -snapshot_directory "${VDIR}/mame" -str ${VMAME_STR}
          -autoboot_script "${T}/mame_snap.lua"
  COMMAND "${CMAKE_COMMAND}" -E touch "${VDIR}/mame/.stamp"
  DEPENDS "${T}/mame_snap.lua" "${INP}" "${VDIR}/vframes.txt" VERBATIM USES_TERMINAL)
add_custom_target(mame-video DEPENDS "${VDIR}/mame/.stamp")
add_custom_target(compare-video
  COMMAND "${Python3_EXECUTABLE}" "${T}/compare_frames.py" --dir-a "${VDIR}" --dir-b "${VDIR}/mame"
          --frames ${SF2_VFRAMES} --diff-dir "${VDIR}/diff"
  DEPENDS png-video "${VDIR}/mame/.stamp" USES_TERMINAL)

# ---- hito 4: sonido -------------------------------------------------------------------------------------------
set(ADIR "${B}/audio")
file(MAKE_DIRECTORY "${ADIR}")
file(CONFIGURE OUTPUT "${ADIR}/bframes.txt" CONTENT "${BFR}\n" @ONLY)
add_custom_target(run-audio
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${ADIR}/recomp"
  COMMAND sf2_blind "${SF2_ROM_BIN}" --frames ${BFR} --inputs "${INP}" --sound-roms "${ROMSRC}"
          --audio-dir "${ADIR}/recomp" --wav "${ADIR}/recomp/sf2.wav" --quiet
  DEPENDS sf2_blind USES_TERMINAL)
add_custom_target(wav-recomp
  COMMAND sf2_blind "${SF2_ROM_BIN}" --frames ${BFR} --inputs "${INP}" --sound-roms "${ROMSRC}"
          --wav "${ADIR}/sf2_recomp.wav" --quiet
  DEPENDS sf2_blind USES_TERMINAL)
add_custom_command(OUTPUT "${ADIR}/mame/.stamp"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${ADIR}/mame"
  COMMAND "${CMAKE_COMMAND}" -E env SF2_AUDIO_DIR=${ADIR}/mame SF2_INPUTS=${INP} SF2_FRAMES=${BFR}
          "${MAME}" sf2 ${MAME_HEADLESS} -str ${MAME_STR} -wavwrite "${ADIR}/mame/sf2_mame.wav"
          -autoboot_script "${T}/mame_audio.lua"
  COMMAND "${CMAKE_COMMAND}" -E touch "${ADIR}/mame/.stamp"
  DEPENDS "${T}/mame_audio.lua" "${INP}" "${ADIR}/bframes.txt" VERBATIM USES_TERMINAL)
add_custom_target(mame-audio DEPENDS "${ADIR}/mame/.stamp")
add_custom_target(compare-audio
  COMMAND "${Python3_EXECUTABLE}" "${T}/compare_audio.py" "${ADIR}/recomp" "${ADIR}/mame" --sections
          --report "${ADIR}/informe_audio.md"
  DEPENDS run-audio "${ADIR}/mame/.stamp" USES_TERMINAL)
add_custom_command(OUTPUT "${ADIR}/cmds_mame.txt"
  COMMAND "${CMAKE_COMMAND}" -E env SF2_CMDS=${ADIR}/cmds_mame.txt SF2_INPUTS=${INP} SF2_FRAMES=${BFR}
          "${MAME}" sf2 ${MAME_HEADLESS} -str ${MAME_STR} -autoboot_script "${T}/mame_sound_cmds.lua"
  DEPENDS "${T}/mame_sound_cmds.lua" "${INP}" "${ADIR}/bframes.txt" VERBATIM USES_TERMINAL)
add_custom_target(compare-sound-cmds
  COMMAND sf2_blind "${SF2_ROM_BIN}" --frames ${BFR} --inputs "${INP}" --sound-cmds "${ADIR}/cmds_recomp.txt" --quiet
  COMMAND "${Python3_EXECUTABLE}" "${T}/compare_sound_cmds.py" "${ADIR}/cmds_recomp.txt" "${ADIR}/cmds_mame.txt"
  DEPENDS sf2_blind "${ADIR}/cmds_mame.txt" USES_TERMINAL)

# ---- hito 6: cobertura con todas las secuencias de tools/sequences/ y validación contra MAME ----------------------
# make coverage: cada secuencia a ciegas (sin red de seguridad de Musashi), cobertura dinámica e informe en
# build/coverage/informe_cobertura.md.  make compare-coverage: además, cada secuencia en MAME (resumen por cuadro,
# comandos de sonido, capturas) e informe en build/coverage/informe_mame.md.  JOBS=N limita los procesos en paralelo.
set(SF2_COVERAGE_JOBS 4 CACHE STRING "Procesos en paralelo de make coverage / compare-coverage")
add_custom_target(coverage
  COMMAND "${Python3_EXECUTABLE}" "${T}/coverage.py" --blind $<TARGET_FILE:sf2_blind> --rom "${SF2_ROM_BIN}"
          --hints "${T}/hints.txt" --sequences "${T}/sequences" --out "${B}/coverage" --jobs ${SF2_COVERAGE_JOBS}
  DEPENDS sf2_blind USES_TERMINAL)
add_custom_target(compare-coverage
  COMMAND "${Python3_EXECUTABLE}" "${T}/coverage.py" --blind $<TARGET_FILE:sf2_blind> --rom "${SF2_ROM_BIN}"
          --hints "${T}/hints.txt" --sequences "${T}/sequences" --out "${B}/coverage" --jobs ${SF2_COVERAGE_JOBS}
          --mame --roms "${ROMSRC}"
  DEPENDS sf2_blind USES_TERMINAL)

# ---- hito 6: biblioteca compartida para tools/playgen.py (ctypes) ----------------------------------------------
add_library(sf2py SHARED sf2py.c)
target_link_libraries(sf2py PRIVATE sf2_core sf2_hostio)
set_target_properties(sf2py PROPERTIES LINKER_LANGUAGE CXX C_VISIBILITY_PRESET hidden
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# En ELF (Linux) los objetos compilados para un ejecutable PIE no pueden ir en una biblioteca compartida (clang de
# Debian compila PIE por omision y el enlace de libsf2py.so fallaba): lo que enlaza libsf2py se compila con -fPIC.
# Mach-O (macOS) es siempre PIC y aqui no cambia nada; Windows y la web no compilan tools/.
if(NOT APPLE AND NOT WIN32)
  set_target_properties(sf2_base sf2_recomp sf2_core sf2_hostio ymfm PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
