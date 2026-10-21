# Ejecuta CMD (lista separada por ;) mostrando su salida e ignora el codigo de salida (para dasmcheck, ver tools/CMakeLists.txt)
#   cmake "-DCMD=programa;arg1;arg2" -P run_ignore_status.cmake
execute_process(COMMAND ${CMD} RESULT_VARIABLE rc)
message(STATUS "(codigo de salida ${rc}, ignorado)")
