#!/usr/bin/env python3
"""Compara píxel a píxel capturas PNG del runtime y de MAME y escribe imágenes de diferencias (hito 3).

Sin dependencias externas: lee y escribe PNG con zlib de la biblioteca estándar (8 bits por canal, RGB o
RGBA, sin entrelazar, filtros 0-4; es lo que escriben MAME y runtime/video.c).

Uso:
  compare_frames.py A.png B.png [--diff salida.png]              # dos archivos
  compare_frames.py --dir-a build/video --dir-b build/video/mame --frames 100,650,...
                    [--prefix-a cuadro --prefix-b cuadro] [--diff-dir build/video/diff] [--max-bad N]

Para cada cuadro imprime píxeles distintos, el rectángulo que los contiene, los primeros píxeles distintos
con el color de cada lado y, si hay diferencias, escribe <diff-dir>/diff_N.png: la captura de A atenuada
con los píxeles distintos en rojo (y en amarillo los que solo difieren de brillo, mismo tono).
Código de salida 0 si todos los cuadros coinciden (o solo difieren en hasta --max-bad píxeles), 1 si no,
2 si falta algún archivo.
"""
import argparse
import os
import struct
import sys
import zlib


def read_png(path):
    """Devuelve (ancho, alto, filas) con cada fila como bytes RGB (3 bytes por píxel)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: no es un PNG")
    pos = 8
    width = height = depth = ctype = interlace = None
    idat = bytearray()
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        ctype_chunk = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype_chunk == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif ctype_chunk == b"IDAT":
            idat += body
        elif ctype_chunk == b"IEND":
            break
    if depth != 8 or ctype not in (2, 6) or interlace != 0:
        raise ValueError(f"{path}: solo se admite PNG de 8 bits RGB/RGBA sin entrelazar (profundidad {depth}, tipo {ctype})")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(height):
        ftype = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xff
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xff
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xff
        elif ftype == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xff
        elif ftype != 0:
            raise ValueError(f"{path}: filtro PNG desconocido {ftype}")
        prev = line
        if bpp == 4:
            rgb = bytearray(width * 3)
            rgb[0::3] = line[0::4]
            rgb[1::3] = line[1::4]
            rgb[2::3] = line[2::4]
            rows.append(bytes(rgb))
        else:
            rows.append(bytes(line))
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff)
    raw = b"".join(b"\x00" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def compare(path_a, path_b, diff_path=None, max_list=8):
    """Devuelve (número de píxeles distintos, texto de informe)."""
    wa, ha, ra = read_png(path_a)
    wb, hb, rb = read_png(path_b)
    if (wa, ha) != (wb, hb):
        return -1, f"tamaños distintos: {wa}x{ha} frente a {wb}x{hb}"
    bad = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    listed = []
    diff_rows = []
    for y in range(ha):
        a, b = ra[y], rb[y]
        if a == b:
            if diff_path:
                diff_rows.append(bytes(v >> 2 for v in a))
            continue
        out = bytearray(v >> 2 for v in a) if diff_path else None
        for x in range(wa):
            i = 3 * x
            pa, pb = a[i:i + 3], b[i:i + 3]
            if pa == pb:
                continue
            bad += 1
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
            if len(listed) < max_list:
                listed.append((x, y, pa.hex(), pb.hex()))
            if out is not None:
                # mismo tono (solo cambia el brillo) en amarillo, el resto en rojo
                same_hue = (pa[0] * pb[1] == pa[1] * pb[0]) and (pa[1] * pb[2] == pa[2] * pb[1])
                out[i:i + 3] = b"\xff\xff\x00" if same_hue else b"\xff\x00\x00"
        if diff_path:
            diff_rows.append(bytes(out))
    if bad == 0:
        return 0, "identico"
    if diff_path:
        write_png(diff_path, wa, ha, diff_rows)
    text = f"{bad} pixeles distintos de {wa * ha} ({100.0 * bad / (wa * ha):.2f}%), rectangulo x={x0}-{x1} y={y0}-{y1}"
    for x, y, ca, cb in listed:
        text += f"\n    ({x},{y}) A=#{ca} B=#{cb}"
    if diff_path:
        text += f"\n    diferencias en {diff_path}"
    return bad, text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="A.png B.png")
    ap.add_argument("--diff", help="imagen de diferencias (modo de dos archivos)")
    ap.add_argument("--dir-a", help="directorio de las capturas del runtime")
    ap.add_argument("--dir-b", help="directorio de las capturas de MAME")
    ap.add_argument("--prefix-a", default="cuadro")
    ap.add_argument("--prefix-b", default="cuadro")
    ap.add_argument("--frames", help="lista de cuadros a,b,c")
    ap.add_argument("--diff-dir", help="directorio para diff_N.png")
    ap.add_argument("--max-bad", type=int, default=0, help="píxeles distintos tolerados por cuadro")
    args = ap.parse_args()

    if args.files:
        if len(args.files) != 2:
            sys.exit("se esperan dos archivos")
        bad, text = compare(args.files[0], args.files[1], args.diff)
        print(f"{os.path.basename(args.files[0])} frente a {os.path.basename(args.files[1])}: {text}")
        return 0 if 0 <= bad <= args.max_bad else 1

    if not (args.dir_a and args.dir_b and args.frames):
        sys.exit("se necesitan --dir-a, --dir-b y --frames (o dos archivos)")
    frames = [int(s) for s in args.frames.split(",") if s.strip()]
    if args.diff_dir:
        os.makedirs(args.diff_dir, exist_ok=True)
    missing = 0
    worst = []
    identical = 0
    for n in frames:
        pa = os.path.join(args.dir_a, f"{args.prefix_a}_{n}.png")
        pb = os.path.join(args.dir_b, f"{args.prefix_b}_{n}.png")
        if not os.path.exists(pa) or not os.path.exists(pb):
            print(f"cuadro {n}: FALTA {'A ' + pa if not os.path.exists(pa) else ''}{'B ' + pb if not os.path.exists(pb) else ''}")
            missing += 1
            continue
        diff = os.path.join(args.diff_dir, f"diff_{n}.png") if args.diff_dir else None
        bad, text = compare(pa, pb, diff)
        tag = "OK" if 0 <= bad <= args.max_bad else "DIFERENCIA"
        print(f"cuadro {n}: {tag}, {text}")
        if bad == 0:
            identical += 1
        if bad < 0 or bad > args.max_bad:
            worst.append((n, bad))
    print(f"\n{identical} de {len(frames) - missing} cuadros identicos a MAME"
          + (f"; distintos: {', '.join(str(n) for n, _ in worst)}" if worst else "")
          + (f"; {missing} sin archivo" if missing else ""))
    if missing:
        return 2
    return 1 if worst else 0


if __name__ == "__main__":
    sys.exit(main())
