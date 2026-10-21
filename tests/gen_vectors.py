#!/usr/bin/env python3
"""Genera tests/vectors.h: texto pseudoaleatorio determinista comprimido con zlib (dinamico, fijo y stored) para
probar el inflate propio (runtime/zipread.c).  Uso: gen_vectors.py > tests/vectors.h"""
import sys
import zlib

vocab = ["street", "fighter", "capcom", "hadoken", "shoryuken", "ryu", "ken", "blanka", "guile", "zangief", " ", "\n"]
x = 12345
out = bytearray()
while len(out) < 4000:
    x = (x * 1103515245 + 12345) & 0xffffffff
    out += vocab[(x >> 16) % 12].encode()
raw = bytes(out[:4000])


def deflate(level, strategy=zlib.Z_DEFAULT_STRATEGY):
    c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
    return c.compress(raw) + c.flush()


def arr(name, b):
    s = "static const uint8_t %s[] = {" % name
    for i, v in enumerate(b):
        if i % 16 == 0:
            s += "\n    "
        s += "0x%02x," % v
    return s + "\n};\n"


sys.stdout.write("/* vectores de prueba de inflate: texto pseudoaleatorio de 4000 bytes (gen_text en unit.c) comprimido con zlib\n"
                 " * de tres formas.  Lo genera tests/gen_vectors.py. */\n")
sys.stdout.write("#define VEC_RAW_LEN %d\n#define VEC_RAW_CRC 0x%08xu\n" % (len(raw), zlib.crc32(raw) & 0xffffffff))
sys.stdout.write(arr("vec_dynamic", deflate(9)) + arr("vec_fixed", deflate(9, zlib.Z_FIXED)) + arr("vec_stored", deflate(0)))
