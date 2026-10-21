# Pruebas (ctest): unitarias sin ROMs, con ROMs (carga, estados guardados), regresiones de los hitos anteriores
# (paso cerrado con Musashi, resumen identico en ejecucion continua y por cuadros) y pureza del nucleo.
add_executable(sf2_unit_tests unit.c)
target_link_libraries(sf2_unit_tests PRIVATE sf2_core sf2_config)
set_target_properties(sf2_unit_tests PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(sf2_unit_tests PRIVATE -Wall -Wno-unused-function)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
  add_test(NAME unit COMMAND sf2_unit_tests)
endif()

add_executable(sf2_rom_tests rom_tests.c)
target_link_libraries(sf2_rom_tests PRIVATE sf2_core sf2_hostio)
set_target_properties(sf2_rom_tests PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(sf2_rom_tests PRIVATE -Wall -Wno-unused-function)
add_executable(sf2_trace trace.c)
target_link_libraries(sf2_trace PRIVATE sf2_core sf2_hostio)
set_target_properties(sf2_trace PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(sf2_trace PRIVATE -Wall)

if(SF2_ROM_DIR AND (NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR))
  add_test(NAME roms_and_states COMMAND sf2_rom_tests "${SF2_ROM_DIR}" "${SF2_INPUTS}")
endif()

if(TARGET sf2_lockstep)
  add_test(NAME lockstep_vs_musashi COMMAND sf2_lockstep "${SF2_ROM_BIN}" --frames ${SF2_FRAMES})
  # el resumen por cuadro es identico corriendo de seguido (machine_run) y cediendo en cada VBlank (machine_step)
  add_test(NAME summary_run_vs_step
    COMMAND "${CMAKE_COMMAND}" -DBLIND=$<TARGET_FILE:sf2_blind> -DROM=${SF2_ROM_BIN} -DINPUTS=${SF2_INPUTS}
            -DOUT=${CMAKE_BINARY_DIR}/test_summary -DFRAMES=3000 -P "${CMAKE_CURRENT_SOURCE_DIR}/summary_run_vs_step.cmake")
endif()

# WebAssembly: las pruebas corren en Node.js (el emulador que Emscripten registra en CMAKE_CROSSCOMPILING_EMULATOR)
# con acceso directo a los archivos del anfitrion, para probar el mismo nucleo compilado a wasm
if(EMSCRIPTEN)
  foreach(t sf2_unit_tests sf2_rom_tests sf2_trace)
    target_link_options(${t} PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=96MB -sSTACK_SIZE=2MB)
  endforeach()
endif()

# el nucleo no llama al sistema (archivos, entorno, relojes, hilos, SDL...)
add_test(NAME core_purity
  COMMAND "${CMAKE_COMMAND}" -DNM=${CMAKE_NM} -DLIBS=$<TARGET_FILE:sf2_base>|$<TARGET_FILE:sf2_core>|$<TARGET_FILE:ymfm>
          -P "${CMAKE_CURRENT_SOURCE_DIR}/check_core_purity.cmake")

# el juego (SDL3) corre sin ventana y escribe los mismos cuadros que el arnes validado contra MAME
if(TARGET sf2 AND TARGET sf2_blind AND SF2_ROM_DIR AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME game_frames_headless
    COMMAND "${CMAKE_COMMAND}" -DGAME=$<TARGET_FILE:sf2> -DBLIND=$<TARGET_FILE:sf2_blind> -DROM=${SF2_ROM_BIN}
            -DROMSRC=${SF2_ROM_DIR} -DINPUTS=${SF2_INPUTS} -DOUT=${CMAKE_BINARY_DIR}/test_game
            -P "${CMAKE_CURRENT_SOURCE_DIR}/game_frames.cmake")
endif()

# un salto a una direccion sin codigo recompilado se registra con un mensaje claro y un estado de rescate (hito 6)
if(TARGET sf2 AND SF2_ROM_DIR AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME game_dispatch_failure
    COMMAND "${CMAKE_COMMAND}" -DGAME=$<TARGET_FILE:sf2> -DROMSRC=${SF2_ROM_DIR} -DOUT=${CMAKE_BINARY_DIR}/test_dispatch_failure
            -P "${CMAKE_CURRENT_SOURCE_DIR}/game_dispatch_failure.cmake")
endif()

# remapeo de teclado y mando con un mando virtual de SDL3
if(TARGET SDL3::SDL3 AND NOT EMSCRIPTEN)
  add_executable(sf2_input_test input_test.c "${SF2_ROOT}/app/input.c")
  target_include_directories(sf2_input_test PRIVATE "${SF2_ROOT}/app" "${SF2_ROOT}/runtime")
  target_link_libraries(sf2_input_test PRIVATE sf2_config SDL3::SDL3)
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME input_mapping COMMAND sf2_input_test)
  endif()
endif()
