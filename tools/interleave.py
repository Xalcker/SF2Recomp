#!/usr/bin/env python3
"""Intercala los ocho ROMs de programa 68000 de SF2 (World 910522) en una imagen plana.

Mapa (direccion 68000 -> par de ROMs), segun ROM_START( sf2 ) de MAME:
  0x00000-0x3FFFF : sf2e_30g.11e (bytes pares) + sf2e_37g.11f (bytes impares)
  0x40000-0x7FFFF : sf2e_31g.12e (pares)       + sf2e_38g.12f (impares)
  0x80000-0xBFFFF : sf2e_28g.9e (pares)        + sf2e_35g.9f (impares)
  0xC0000-0xFFFFF : sf2_29b.10e (pares)        + sf2_36b.10f (impares)   (solo datos, sin codigo)
Total 0x100000 bytes (1 MiB).  El 68000 es big-endian: el byte par es el byte alto de cada palabra.

Las ROMs se identifican por CRC-32 y tamano, no por nombre (hito 5): el origen puede ser una carpeta con los
archivos sueltos (nombres antiguos como sf2e.30g / sf2_29a.bin o actuales) o el zip de MAME (sf2.zip), con
cualquier nombre.  La misma tabla esta en runtime/romset.c.

La imagen de salida contiene codigo de Capcom: no la guardes en el repositorio (build/ esta en .gitignore).

Uso: interleave.py <carpeta_o_zip> <salida.bin>
"""
import hashlib
import os
import sys
import zlib

ROM_SIZE = 0x20000      # 128 KiB por chip
TOTAL = 0x100000
# (nombre actual, nombre antiguo, CRC-32, base, 0 = pares / 1 = impares)
PROGRAM = [
    ("sf2e_30g.11e", "sf2e.30g",    0xfe39ee33, 0x00000, 0),
    ("sf2e_37g.11f", "sf2e.37g",    0xfb92cd74, 0x00000, 1),
    ("sf2e_31g.12e", "sf2e.31g",    0x69a0a301, 0x40000, 0),
    ("sf2e_38g.12f", "sf2e.38g",    0x5e22db70, 0x40000, 1),
    ("sf2e_28g.9e",  "sf2e.28g",    0x8bf9f1e5, 0x80000, 0),
    ("sf2e_35g.9f",  "sf2e.35g",    0x626ef934, 0x80000, 1),
    ("sf2_29b.10e",  "sf2_29a.bin", 0xbb4af315, 0xC0000, 0),
    ("sf2_36b.10f",  "sf2_36a.bin", 0xc02a13eb, 0xC0000, 1),
]


def candidates(src):
    """Genera (nombre, bytes) de los archivos de 128 KiB de la carpeta o del zip."""
    if os.path.isdir(src):
        for name in sorted(os.listdir(src)):
            path = os.path.join(src, name)
            if os.path.isfile(path) and os.path.getsize(path) == ROM_SIZE:
                with open(path, "rb") as f:
                    yield name, f.read()
    else:
        import zipfile
        if not zipfile.is_zipfile(src):
            sys.exit(f"{src}: no es una carpeta ni un zip")
        with zipfile.ZipFile(src) as z:
            for info in z.infolist():
                if info.file_size == ROM_SIZE:
                    yield info.filename, z.read(info)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    img = bytearray(TOTAL)
    found = {}
    for name, data in candidates(src):
        crc = zlib.crc32(data) & 0xffffffff
        for i, (_, _, want, base, lane) in enumerate(PROGRAM):
            if crc == want and i not in found:
                found[i] = name
                img[base + lane:base + 2 * ROM_SIZE:2] = data
                break
    missing = [PROGRAM[i] for i in range(len(PROGRAM)) if i not in found]
    if missing:
        lines = [f"  - {n} (o {a}), 128 KiB, CRC {c:08x}" for n, a, c, _, _ in missing]
        sys.exit(f"Faltan {len(missing)} de las 8 ROMs de programa de SF2 (World 910522, set \"sf2\" de MAME) en {src}:\n"
                 + "\n".join(lines))
    with open(out, "wb") as f:
        f.write(img)
    ssp = int.from_bytes(img[0:4], "big")
    pc = int.from_bytes(img[4:8], "big")
    irq2 = int.from_bytes(img[0x68:0x6C], "big")
    print(f"escrito {out}: {len(img):#x} bytes, sha1={hashlib.sha1(img).hexdigest()}, crc32={zlib.crc32(img) & 0xffffffff:08x}")
    print(f"vector reset: SSP={ssp:#010x} PC={pc:#010x}  IRQ2(autovector 0x68)={irq2:#010x}")


if __name__ == "__main__":
    main()
