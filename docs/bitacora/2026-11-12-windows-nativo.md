# 2026-11-12 · Primera compilación en un Windows real y revisión del código (D70)

## Pregunta de partida

Compilar *Hyper Fighting* (`sf2hf`) de forma nativa en Windows, que hasta ahora solo se había probado por compilación cruzada y con Wine
(`2026-10-20-hito6-plataformas`), y de paso revisar el código en busca de problemas.

## Entorno

Windows 11 Pro x86-64. GCC 16.2.0 del entorno UCRT64 de MSYS2, CMake 4.4.4 y Ninja 1.13.2 (de `winget`), Python 3.13 y SDL3 3.4.18
(`SDL3-devel-3.4.18-mingw.tar.gz`, el mismo archivo y SHA-256 de la compilación cruzada) en `external/sdl3-mingw`. ROMs en la carpeta
`sf2hf/`. El clang de LLVM instalado en la máquina apunta a `x86_64-pc-windows-msvc` y no se probó.

```bash
tools/setup-local-tools.sh sdl3-mingw
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake -S . -B build-hf -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_PREFIX_PATH="$PWD/external/sdl3-mingw/x86_64-w64-mingw32" -DSF2_GAME=sf2hf -DSF2_ROM_DIR=C:/.../sf2hf
cmake --build build-hf && ctest --test-dir build-hf
```

## Qué se encontró y qué se hizo

| Problema | Dónde | Arreglo |
|---|---|---|
| `render_dump.exe` no enlazaba: `undefined reference to rt_hook / rt_dispatch / rt_fatal`. El código generado (`sf2_recomp`) llama a funciones de `machine.c` (`sf2_core`) y `sf2_core` depende de `sf2_recomp`; GNU ld recorre las bibliotecas una vez y, si el ejecutable no usa la máquina por su cuenta (`render_dump` solo pide `recomp_game` y el vídeo), no las encuentra. El enlazador de macOS no depende del orden y no lo mostraba | `runtime/CMakeLists.txt` | La dependencia circular se declara (`target_link_libraries(sf2_recomp INTERFACE sf2_core)`) y CMake repite las bibliotecas |
| `sf2.exe` pedía `libstdc++-6.dll` y `libgcc_s_seh-1.dll`: solo arranca con MSYS2 en el `PATH` (o carga las de otro programa, como las de Git) | `CMakeLists.txt` | Con MinGW nativo se enlaza con `-static`; quedan `KERNEL32.dll`, el UCRT de Windows y `SDL3.dll` |
| Con una ñ o un acento en la ruta de la configuración no se podía escribir (`no se pudo escribir ...\cañón\sf2.ini`): SDL da las rutas en UTF-8 y `fopen`/`remove`/`rename` las leen en ANSI. Le pasa a cualquier usuario de Windows con un acento en el nombre | `platform/hostio.c`, `wav.c`, `app/main.c` | `hostio_fopen` y las funciones anchas de Windows; el reemplazo atómico es `MoveFileExW` (antes se borraba el archivo y luego se renombraba el temporal) |
| Los estados guardados se llamaban `sf2_slotN.sta` en las tres variantes, que comparten la carpeta de preferencias: guardar en una pisaba la ranura de otra (que luego rechazaba el estado por el CRC del código) | `app/main.c` | `<set>_slotN.sta`; los de WW conservan el nombre |
| `errores.log` mandaba añadir la pista siempre a `tools/hints.txt` | `app/main.c` | El archivo de la variante (`tools/hints_<set>.txt`), como ya hacía el mensaje del núcleo |
| El mensaje de `wide_width` no válido decía «de 384 a 512» (el techo es 800) y dejaba `widescreen` encendido en la configuración | `app/main.c`, `runtime/video.h` | Mensaje con los límites reales y `widescreen` apagado |
| GCC ignora `#pragma STDC FP_CONTRACT OFF` (aviso `-Wunknown-pragmas`): en arm64, donde GCC contrae a FMA por omisión, la tabla del remuestreador podía salir distinta de la de MAME | `runtime/sound.c` | En GCC, `__attribute__((optimize("fp-contract=off")))` en `interp_init`; clang sigue con el pragma |
| `static const char *yn(int)` choca con la función de Bessel `yn` de `<math.h>` (aviso de GCC) | `platform/config.c` | Renombrada a `bool_text` |
| `docs/VARIANTES.md` decía que `mod_patches.txt` solo se aplica a WW (desde D68 hay uno por variante) | `docs/VARIANTES.md` | Corregido, y anotado que el `.ini` y `[paths] rom` son comunes a las tres |

`tools/setup-local-tools.sh` tiene un subcomando nuevo, `sdl3-mingw`, que funciona en Git Bash. La franja del borde izquierdo de la pantalla
ancha (incidencia #3) está en su propia entrada, `2026-11-12-pantalla-ancha-borde-izquierdo`.

## Resultado

- Compilación desde cero en una carpeta nueva con los comandos del README: 0 errores; los 11 ejecutables y `libsf2py.dll` enlazan.
- `ctest`: 8/8 (`unit`, `roms_and_states` con las pruebas de pantalla ancha, lógica ancha y stage completo, `lockstep_vs_musashi`,
  `summary_run_vs_step`, `core_purity`, `game_frames_headless`, `game_dispatch_failure`, `input_mapping`), unos 140 s.
- `sf2.exe` corre con `PATH=C:\Windows\System32` (sin MSYS2): 2.001 cuadros sin ventana a 512 px y su captura.
- `--write-config` en una carpeta con ñ y acento: crea el archivo y lo sobrescribe.

## Avisos del compilador que se dejan

Con GCC 16 quedan los de `-Wformat-truncation` en rutas (`app/main.c`: los búferes son de 1.024 a 3.200 bytes y `snprintf` trunca sin
desbordar) y en el desensamblador del Z80 (`runtime/z80.c:758`, 16 bytes para un texto de 8 como mucho), un `-Wmisleading-indentation`
del mismo desensamblador (el `break` va después del `if`/`else` y es correcto) y dos de Musashi (`third_party`).

## Pendiente

- No se comparó con MAME en Windows (`compare-*` necesitan el MAME local de `external/mame` y `tools/mame.sh`, que son de macOS).
- ~~WW y CE no se compilaron aquí~~: el 2026-11-13, ya con sus ROMs, `main` (PR #7) compila en Windows con `SF2_GAME=sf2` y `sf2ce` y
  pasa `ctest` 8/8 en las dos. Sigue faltando `make test` en macOS, sobre todo `sound.c` con GCC en arm64 si se usa ese compilador.
- El audio no se comparó bit a bit: en x86-64 no hay FMA por omisión y ya se sabía que difiere en 1 LSB en unas pocas muestras (D33).
- Probar `tools/windows-wine-docker.sh` y la cadena cruzada tras el cambio de enlace (la cadena cruzada ya pasaba sus propias opciones
  estáticas y no recibe `-static`).
