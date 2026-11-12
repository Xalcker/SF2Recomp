#!/usr/bin/env python3
"""Monta la imagen plana del programa 68000 de una variante de SF2 a partir de sus ROMs.

sf2 (World 910522), segun ROM_START( sf2 ) de MAME:
  0x00000-0x3FFFF : sf2e_30g.11e (bytes pares) + sf2e_37g.11f (bytes impares)
  0x40000-0x7FFFF : sf2e_31g.12e (pares)       + sf2e_38g.12f (impares)
  0x80000-0xBFFFF : sf2e_28g.9e (pares)        + sf2e_35g.9f (impares)
  0xC0000-0xFFFFF : sf2_29b.10e (pares)        + sf2_36b.10f (impares)   (solo datos, sin codigo)
  Total 0x100000 bytes (1 MiB).  El 68000 es big-endian: el byte par es el byte alto de cada palabra.
sf2ce (World 920513): tres ROMs de palabras de 512 KiB (s92e_23b.8f, s92_22b.7f, s92_21a.6f) seguidas, 1,5 MiB, con
  los dos bytes de cada palabra invertidos en el archivo (ROM_LOAD16_WORD_SWAP).
Las tablas estan en tools/games.py (espejo de runtime/game.c).

Las ROMs se identifican por CRC-32 y tamano, no por nombre (hito 5): el origen puede ser una carpeta con los
archivos sueltos (nombres antiguos o actuales) o el zip de MAME, con cualquier nombre.

La imagen de salida contiene codigo de Capcom: no la guardes en el repositorio (build/ esta en .gitignore).

Uso: interleave.py <carpeta_o_zip> <salida.bin> [--game sf2|sf2ce]
"""
import argparse
import hashlib
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from games import game, build_program  # noqa: E402


def candidates(src, sizes):
    """Genera (nombre, bytes) de los archivos de la carpeta o del zip con alguno de los tamanos dados."""
    if os.path.isdir(src):
        for name in sorted(os.listdir(src)):
            path = os.path.join(src, name)
            if os.path.isfile(path) and os.path.getsize(path) in sizes:
                with open(path, "rb") as f:
                    yield name, f.read()
    else:
        import zipfile
        if not zipfile.is_zipfile(src):
            sys.exit(f"{src}: no es una carpeta ni un zip")
        with zipfile.ZipFile(src) as z:
            for info in z.infolist():
                if info.file_size in sizes:
                    yield info.filename, z.read(info)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--game", default="sf2", help="set de MAME (sf2, sf2ce)")
    args = ap.parse_args()
    g = game(args.game)
    program = g["program"]
    sizes = {p[3] for p in program}
    found, chips = {}, {}
    for name, data in candidates(args.src, sizes):
        crc = zlib.crc32(data) & 0xffffffff
        for i, (_, _, want, size, _, _) in enumerate(program):
            if crc == want and len(data) == size and i not in found:
                found[i] = name
                chips[i] = data
                break
    missing = [program[i] for i in range(len(program)) if i not in found]
    if missing:
        lines = [f"  - {n}{'' if a == n else f' (o {a})'}, {s // 1024} KiB, CRC {c:08x}" for n, a, c, s, _, _ in missing]
        sys.exit(f"Faltan {len(missing)} de las {len(program)} ROMs de programa de {g['title']} (set \"{args.game}\" de MAME) "
                 f"en {args.src}:\n" + "\n".join(lines))
    img = build_program(args.game, chips)
    with open(args.out, "wb") as f:
        f.write(img)
    ssp = int.from_bytes(img[0:4], "big")
    pc = int.from_bytes(img[4:8], "big")
    irq2 = int.from_bytes(img[0x68:0x6C], "big")
    print(f"escrito {args.out} ({args.game}): {len(img):#x} bytes, sha1={hashlib.sha1(img).hexdigest()}, "
          f"crc32={zlib.crc32(img) & 0xffffffff:08x}")
    print(f"vector reset: SSP={ssp:#010x} PC={pc:#010x}  IRQ2(autovector 0x68)={irq2:#010x}")


if __name__ == "__main__":
    main()
