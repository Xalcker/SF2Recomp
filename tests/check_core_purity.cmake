# check_core_purity.cmake - el nucleo (runtime/) no debe llamar al sistema: se listan los simbolos sin definir de
# sus bibliotecas y se rechaza cualquiera de la lista de prohibidos (archivos por ruta, entorno, procesos, relojes,
# hilos, SDL...).  Quedan permitidos la memoria dinamica, string.h, setjmp/longjmp, math y los flujos FILE* que
# recibe de quien lo usa para los registros de validacion (fprintf, fwrite).
#   cmake -DNM=<nm> -DLIBS="a.a|b.a" -P check_core_purity.cmake
if(NOT NM)
  set(NM nm)
endif()
string(REPLACE "|" ";" LIB_LIST "${LIBS}")
set(banned
  fopen freopen fdopen tmpfile fseek ftell remove rename getenv setenv putenv system popen exit _exit abort
  signal raise time clock clock_gettime gettimeofday nanosleep usleep sleep perror printf puts putchar
  open openat read write close stat fstat mmap munmap dlopen dlsym opendir readdir
  __stdoutp __stderrp __stdinp stdout stderr stdin)
set(bad "")
foreach(lib ${LIB_LIST})
  execute_process(COMMAND "${NM}" -u "${lib}" OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "nm -u ${lib}: ${err}")
  endif()
  string(REPLACE "\n" ";" lines "${out}")
  foreach(line ${lines})
    string(STRIP "${line}" sym)
    if(sym STREQUAL "" OR sym MATCHES ":$")
      continue()
    endif()
    string(REGEX REPLACE "^_+" "" bare "${sym}")           # quita los guiones bajos de macOS/Mach-O
    if(bare MATCHES "^SDL_" OR bare MATCHES "^pthread_")
      list(APPEND bad "${sym} (${lib})")
      continue()
    endif()
    foreach(b ${banned})
      string(REGEX REPLACE "^_+" "" bb "${b}")
      if(bare STREQUAL bb)
        list(APPEND bad "${sym} (${lib})")
      endif()
    endforeach()
  endforeach()
endforeach()
if(bad)
  list(REMOVE_DUPLICATES bad)
  string(REPLACE ";" "\n  " msg "${bad}")
  message(FATAL_ERROR "El nucleo llama al sistema:\n  ${msg}")
endif()
message(STATUS "core_purity: sin llamadas al sistema en ${LIB_LIST}")
