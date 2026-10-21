#!/usr/bin/env python3
"""hitbox_probe.py - lee las cajas de golpe y de daño de la RAM del juego y las dibuja sobre la captura (docs/HITBOXES.md).

Reproduce una secuencia de entradas (tools/sequences/*.txt) hasta un cuadro, lee de la RAM de trabajo y de la ROM de
programa (build/rom.bin) las cajas de los dos luchadores y de los proyectiles con la especificación de docs/HITBOXES.md,
y las dibuja como rectángulos sobre los píxeles 0x00RRGGBB de sf2py_rgb.  Solo biblioteca estándar (ctypes, zlib, struct).

Uso:
  python3 -I tools/hitbox_probe.py --seq tools/sequences/ryu.txt --frame 1705 --out /ruta/salida.png [--list] [--zoom 2]
  python3 -I tools/hitbox_probe.py --seq ... --frames 1700,1705 --out /ruta/salida_%d.png

Colores: cabeza amarillo, cuerpo verde, pies azul, punto débil magenta, ataque rojo, empuje/«atari» blanco.
La ROM (build/rom.bin) y los PNG con capturas del juego no se guardan en el repositorio.
"""
import argparse
import ctypes
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import playgen as pg  # noqa: E402

# ---- constantes de la especificación (ver docs/HITBOXES.md) ------------------------------------------------------
PLAYER_BASE = (0xFF83C0, 0xFF86C0)      # estructura de jugador; el «objeto» (a6 del juego) empieza 6 bytes después
OBJ_OFF = 6
CAM_X, CAM_Y = 0xFF8BD8, 0xFF8BDC       # cámara de mundo (palabras): pantalla x = x - CAM_X ; y = 240 - (y - CAM_Y)
SCREEN_Y0 = 240
PROJ_LINK = 0x1D4                       # puntero (palabra) al proyectil del jugador, relativo al objeto
POOLS = {                               # reservas de objetos (base, cuántos, tamaño); la C trae los proyectiles
    "A": (0xFF918A, 2, 0x100), "B": (0xFF998A, 16, 0xC0), "C": (0xFF938A, 8, 0xC0), "D": (0xFFA58A, 60, 0xC0)}
KINDS = ("head", "body", "foot", "weak", "attack", "push")
COLORS = {"head": 0xFFFF00, "body": 0x00FF00, "foot": 0x3060FF, "weak": 0xFF00FF, "attack": 0xFF0000, "push": 0xFFFFFF}

ROM = None


def rom():
    global ROM
    if ROM is None:
        ROM = open(os.path.join(ROOT, "build", "rom.bin"), "rb").read()
    return ROM


def rb(a):
    return rom()[a]


def rw(a):
    r = rom()
    return (r[a] << 8) | r[a + 1]


def rl(a):
    r = rom()
    return (r[a] << 24) | (r[a + 1] << 16) | (r[a + 2] << 8) | r[a + 3]


def s8(v):
    return v - 256 if v & 0x80 else v


class Box:
    __slots__ = ("kind", "owner", "cx", "cy", "rx", "ry", "idx", "rec")

    def __init__(self, kind, owner, cx, cy, rx, ry, idx, rec):
        self.kind, self.owner, self.cx, self.cy, self.rx, self.ry, self.idx, self.rec = kind, owner, cx, cy, rx, ry, idx, rec

    def screen(self, camx, camy):
        """rectángulo en píxeles de pantalla (x0, y0, x1, y1), extremos inclusive"""
        x = self.cx - camx
        y = SCREEN_Y0 - (self.cy - camy)
        return (x - self.rx, y - self.ry, x + self.rx, y + self.ry)


def object_boxes(m, obj, owner):
    """cajas de un objeto (a6 del juego = dirección de la RAM `obj`); lista vacía si no existe"""
    if m.b(obj) == 0:
        return []
    fp = (m.w(obj + 0x1A) << 16) | m.w(obj + 0x1C)          # puntero a la entrada de la pose actual (ROM)
    tab = (m.w(obj + 0x34) << 16) | m.w(obj + 0x36)         # puntero a la tabla de cajas del personaje (ROM)
    if not (0 < fp < 0x100000 and 0 < tab < 0x100000):
        return []
    x, y = m.sw(obj + 6), m.sw(obj + 0xA)
    flip = m.b(obj + 0x12) != 0                             # != 0: mira a la derecha, se niegan los desplazamientos x
    out = []

    def px(off):
        return x + (-off if flip else off)

    for kind, byte, hoff in (("head", 8, 0), ("body", 9, 2), ("foot", 10, 4), ("weak", 11, 6), ("push", 13, 10)):
        idx = rb(fp + byte)
        if idx:
            a = tab + rw(tab + hoff) + idx * 4
            out.append(Box(kind, owner, px(s8(rb(a))), y + s8(rb(a + 1)), rb(a + 2), rb(a + 3), idx, bytes(rom()[a:a + 4])))
    idx = rb(fp + 12)
    if idx:                                                 # ataque: registro de 12 bytes
        a = tab + rw(tab + 8) + idx * 12
        rec = bytes(rom()[a:a + 12])
        b5 = rec[5]
        off = -b5 if b5 & 0x80 else s8(rec[0])             # byte 5 con bit 7: alcance largo, x = -byte5 (sin signo)
        out.append(Box("attack", owner, px(off), y + s8(rec[1]), rec[2], rec[3], idx, rec))
    return out


def all_boxes(m):
    """cajas de los dos luchadores y de los proyectiles activos (los que cuelgan de +0x1D4 de cada jugador)"""
    boxes, objs = [], []
    for i, pb in enumerate(PLAYER_BASE):
        o = pb + OBJ_OFF
        objs.append(("P%d" % (i + 1), o))
        boxes += object_boxes(m, o, "P%d" % (i + 1))
        link = m.w(o + PROJ_LINK)
        if 0xFF0000 < 0xFF0000 + link and link >= 0x8000:
            po = 0xFF0000 + link
            if m.b(po) == 1:
                objs.append(("P%d-proy" % (i + 1), po))
                boxes += object_boxes(m, po, "P%d-proy" % (i + 1))
    return boxes, objs


# ---- dibujo y PNG ----------------------------------------------------------------------------------------------
def grab(m):
    lib = m.lib
    lib.sf2py_rgb.restype = ctypes.POINTER(ctypes.c_uint32)
    lib.sf2py_rgb.argtypes = [ctypes.c_void_p]
    p = lib.sf2py_rgb(m.s)
    return [p[i] for i in range(384 * 224)], 384, 224


def draw_rect(px, w, h, r, color):
    x0, y0, x1, y1 = r
    for x in range(x0, x1 + 1):
        for y in (y0, y1):
            if 0 <= x < w and 0 <= y < h:
                px[y * w + x] = color
    for y in range(y0, y1 + 1):
        for x in (x0, x1):
            if 0 <= x < w and 0 <= y < h:
                px[y * w + x] = color


def write_png(path, px, w, h, zoom=1):
    raw = bytearray()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            c = px[y * w + x]
            row += bytes(((c >> 16) & 255, (c >> 8) & 255, c & 255)) * zoom
        for _ in range(zoom):
            raw += b"\x00" + row

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * zoom, h * zoom, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def render(m, path, zoom=1, kinds=KINDS, cross=False):
    px, w, h = grab(m)
    boxes, _ = all_boxes(m)
    camx, camy = m.sw(CAM_X), m.sw(CAM_Y)
    for b in boxes:
        if b.kind in kinds:
            draw_rect(px, w, h, tuple(int(v) for v in b.screen(camx, camy)), COLORS[b.kind])
    write_png(path, px, w, h, zoom)


# ---- reproducción ----------------------------------------------------------------------------------------------
def load_sequence(path):
    ev = {}
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        a, b, k = line.split()
        ev.setdefault(k, []).append((int(a), int(b)))

    def bits(f):
        r = [0, 0, 0]
        for k, l in ev.items():
            for a, b in l:
                if a <= f <= b:
                    p, mk = pg.KEYS[k]
                    r[p] |= mk
        return tuple(r)
    return bits


def make_machine(video=True):
    m = pg.Machine(os.path.join(ROOT, "build", "libsf2py.dylib"), os.path.join(ROOT, "sf2.zip"), video=video)
    m.set_dips(0xFF, 0xFC, 0x9F)
    return m


def describe(m):
    boxes, objs = all_boxes(m)
    camx, camy = m.sw(CAM_X), m.sw(CAM_Y)
    print("cuadro %d  cámara x=%d y=%d" % (m.frame(), camx, camy))
    for name, o in objs:
        print("  %-8s obj 0x%06X x=%d y=%d flip=%d pose=0x%05X tabla=0x%05X" % (
            name, o, m.sw(o + 6), m.sw(o + 0xA), m.b(o + 0x12), (m.w(o + 0x1A) << 16) | m.w(o + 0x1C),
            (m.w(o + 0x34) << 16) | m.w(o + 0x36)))
    for b in boxes:
        print("  %-8s %-6s idx=%-3d centro=(%d,%d) radios=(%d,%d) pantalla=%s reg=%s" % (
            b.owner, b.kind, b.idx, b.cx, b.cy, b.rx, b.ry, tuple(int(v) for v in b.screen(camx, camy)), b.rec.hex()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seq", required=True, help="archivo de secuencia (tools/sequences/*.txt)")
    ap.add_argument("--frame", type=int, help="cuadro a capturar")
    ap.add_argument("--frames", help="lista de cuadros separados por comas (--out con %%d)")
    ap.add_argument("--out", required=True, help="PNG de salida (con %%d si hay varios cuadros)")
    ap.add_argument("--zoom", type=int, default=2)
    ap.add_argument("--list", action="store_true", help="imprime las cajas leídas")
    a = ap.parse_args()
    frames = sorted(int(x) for x in a.frames.split(",")) if a.frames else [a.frame]
    m = make_machine()
    bits = load_sequence(a.seq)
    for f in frames:
        while m.frame() < f:
            m.step(bits(m.frame() + 1))
        out = a.out % f if "%" in a.out else a.out
        if a.list:
            describe(m)
        render(m, out, a.zoom)
        print("escrito", out)


if __name__ == "__main__":
    main()
