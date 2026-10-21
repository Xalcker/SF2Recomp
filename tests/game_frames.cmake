# El juego (SDL3, sin ventana con SDL_VIDEODRIVER=dummy, ROMs leidas de sf2.zip o de la carpeta) escribe los mismos PNG
# que el arnes sf2_blind, que a su vez es identico a MAME pixel a pixel (make compare-video).
#   cmake -DGAME=... -DBLIND=... -DROM=... -DROMSRC=... -DINPUTS=... -DOUT=... -P game_frames.cmake
set(FRAMES 100,650,900,1100,1250,1600,1800,2200,3000)
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/game" "${OUT}/blind")
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_AUDIODRIVER} dummy)
execute_process(COMMAND "${GAME}" "${ROMSRC}" --quiet --nothrottle --mute --config "${OUT}/sf2.ini" --inputs "${INPUTS}" --frames 3001
                --png-frames ${FRAMES} --png-dir "${OUT}/game" RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sf2 fallo (${rc}): ${err}")
endif()
execute_process(COMMAND "${BLIND}" "${ROM}" --frames 3001 --inputs "${INPUTS}" --gfx "${ROMSRC}" --png-frames ${FRAMES}
                --png-dir "${OUT}/blind" --quiet RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sf2_blind fallo (${rc}): ${err}")
endif()
string(REPLACE "," ";" LIST "${FRAMES}")
foreach(n ${LIST})
  file(SHA256 "${OUT}/game/cuadro_${n}.png" a)
  file(SHA256 "${OUT}/blind/cuadro_${n}.png" b)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "cuadro ${n}: el juego y el arnes difieren")
  endif()
endforeach()
list(LENGTH LIST count)
message(STATUS "game_frames: ${count} cuadros identicos entre el juego y el arnes")
