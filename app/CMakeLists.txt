# El juego: SDL3 es la unica dependencia externa de plataforma (ventana, teclado y mandos, audio).
if(EMSCRIPTEN)
  set(SF2_SDL_PORT "--use-port=sdl3")
else()
  find_package(SDL3 CONFIG QUIET GLOBAL)       # GLOBAL: tests/ usa el mismo destino importado
  if(NOT TARGET SDL3::SDL3)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(SDL3_PC IMPORTED_TARGET GLOBAL sdl3)
      if(TARGET PkgConfig::SDL3_PC)
        add_library(SDL3::SDL3 INTERFACE IMPORTED GLOBAL)
        target_link_libraries(SDL3::SDL3 INTERFACE PkgConfig::SDL3_PC)
      endif()
    endif()
  endif()
  if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR
      "No se encuentra SDL3 (>= 3.2). Instalala (macOS: brew install sdl3; Debian/Ubuntu: libsdl3-dev) o "
      "indica -DCMAKE_PREFIX_PATH=/donde/esta/SDL3, o compila sin el juego con -DSF2_BUILD_APP=OFF.")
  endif()
endif()

add_executable(sf2 main.c input.c)
target_include_directories(sf2 PRIVATE "${SF2_ROOT}/runtime")
target_link_libraries(sf2 PRIVATE sf2_core sf2_hostio sf2_config)
set_target_properties(sf2 PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(sf2 PRIVATE -Wall -Wno-unused-function)
if(EMSCRIPTEN)
  # Pagina web (app/web/shell.html): pide sf2.zip con un selector de archivo, lo escribe en el sistema de archivos
  # en memoria y llama a main; la configuracion y los estados guardados van a IndexedDB (/persist).
  target_compile_options(sf2 PRIVATE ${SF2_SDL_PORT})
  target_link_options(sf2 PRIVATE ${SF2_SDL_PORT}
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=96MB -sSTACK_SIZE=2MB
    -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -lidbfs.js
    "-sEXPORTED_RUNTIME_METHODS=['callMain','FS','IDBFS','UTF8ToString']"
    "--embed-file=${SF2_ROOT}/tools/inputs_sf2.txt@/inputs_sf2.txt")
  set_target_properties(sf2 PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
  set_property(TARGET sf2 APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html" "${SF2_ROOT}/tools/inputs_sf2.txt")
else()
  target_link_libraries(sf2 PRIVATE SDL3::SDL3)
endif()
if(WIN32)
  # SDL3.dll junto al .exe (el resto del runtime va enlazado dentro)
  add_custom_command(TARGET sf2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:sf2> $<TARGET_FILE_DIR:sf2>
    COMMAND_EXPAND_LISTS)
endif()

# make run-video: el juego con la partida de la secuencia de entradas (ventana y sonido)
if(NOT CMAKE_CROSSCOMPILING)
  add_custom_target(run-video COMMAND sf2 "${SF2_ROM_DIR}" --inputs "${SF2_INPUTS}" --frames ${SF2_BFRAMES}
                    DEPENDS sf2 USES_TERMINAL)
endif()
