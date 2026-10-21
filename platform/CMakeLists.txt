# Servicios del anfitrion sin SDL: archivos (stdio), carga de ROMs desde carpeta o zip, PNG y WAV.
add_library(sf2_hostio STATIC hostio.c wav.c)
target_include_directories(sf2_hostio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(sf2_hostio PUBLIC sf2_base)
target_compile_options(sf2_hostio PRIVATE -Wall)

# configuracion (INI) y entradas: sin SDL tampoco, para poder probarlas en las pruebas
add_library(sf2_config STATIC config.c)
target_include_directories(sf2_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(sf2_config PRIVATE -Wall)
