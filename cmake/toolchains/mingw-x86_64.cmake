# Compilacion cruzada a Windows x86-64 con llvm-mingw (clang + lld + UCRT), instalado en external/llvm-mingw.
# Las bibliotecas de SDL3 para mingw (SDL3-devel-*-mingw.tar.gz) van en external/sdl3-mingw.
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake -DSF2_ROM_DIR=/ruta/sf2.zip
# Para Windows arm64 basta cambiar SF2_MINGW_ARCH a aarch64 (y usar el SDL3 de aarch64 si hay).
set(CMAKE_SYSTEM_NAME Windows)
if(NOT SF2_MINGW_ARCH)
  set(SF2_MINGW_ARCH x86_64)
endif()
set(CMAKE_SYSTEM_PROCESSOR ${SF2_MINGW_ARCH})
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LLVM_MINGW_DIR "${_root}/external/llvm-mingw" CACHE PATH "Instalacion local de llvm-mingw")
set(SDL3_MINGW_DIR "${_root}/external/sdl3-mingw/${SF2_MINGW_ARCH}-w64-mingw32" CACHE PATH "SDL3 para mingw")
set(_triple ${SF2_MINGW_ARCH}-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_DIR}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_DIR}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_DIR}/bin/${_triple}-windres")
set(CMAKE_AR "${LLVM_MINGW_DIR}/bin/${_triple}-ar")
set(CMAKE_RANLIB "${LLVM_MINGW_DIR}/bin/${_triple}-ranlib")
set(CMAKE_NM "${LLVM_MINGW_DIR}/bin/${_triple}-nm")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_DIR}/${_triple}" "${SDL3_MINGW_DIR}")
set(CMAKE_PREFIX_PATH "${SDL3_MINGW_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# el runtime de C++ y de GCC va enlazado dentro del .exe: solo hace falta SDL3.dll junto a el
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
