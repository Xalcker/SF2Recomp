# El resumen por cuadro es byte a byte el mismo ejecutando de seguido (machine_run) que cediendo el control tras
# cada VBlank (machine_step, como el juego SDL3 y el navegador).
#   cmake -DBLIND=... -DROM=... -DINPUTS=... -DOUT=... -DFRAMES=... -P summary_run_vs_step.cmake
file(MAKE_DIRECTORY "${OUT}")
foreach(mode run step)
  set(args "")
  if(mode STREQUAL "step")
    set(args --step)
  endif()
  execute_process(COMMAND "${BLIND}" "${ROM}" --frames ${FRAMES} --inputs "${INPUTS}" --summary "${OUT}/${mode}.txt" --quiet ${args}
                  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sf2_blind ${mode} fallo (${rc}): ${err}")
  endif()
endforeach()
file(SHA256 "${OUT}/run.txt" h1)
file(SHA256 "${OUT}/step.txt" h2)
if(NOT h1 STREQUAL h2)
  message(FATAL_ERROR "los resumenes difieren: run ${h1} step ${h2}")
endif()
message(STATUS "resumen de ${FRAMES} cuadros identico (run == step): ${h1}")
