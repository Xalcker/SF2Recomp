# ymfm (subconjunto para el YM2151)

Copia sin modificar de cinco archivos de ymfm, de Aaron Giles (licencia BSD-3, ver `LICENSE`):
`src/ymfm.h`, `src/ymfm_fm.h`, `src/ymfm_fm.ipp`, `src/ymfm_opm.h` y `src/ymfm_opm.cpp`.

Origen: `3rdparty/ymfm` del árbol de MAME 0.289 (commit f34f02505e32c1993c6a782b6814232cbfc74e36), el mismo
código que usa el YM2151 de la referencia; proyecto original en https://github.com/aaronsgiles/ymfm.

Solo lo usa `runtime/ym2151.cpp`, que implementa `ymfm_interface` (temporizadores, ocupado, sincronización)
como el dispositivo de MAME (decisión D28).  Se compila con `-std=c++17`.
