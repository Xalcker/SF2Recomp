"""Intercala los ROMs de programa 68000 de SF2 (World 910522) y los inspecciona.

Uso: python3 -I tools/probe_rom.py <carpeta_roms> [salida.bin]
La imagen de salida contiene código de Capcom: no la guardes en el repositorio.
"""
import sys, struct, hashlib, os, re

d = sys.argv[1]
def rd(n): return open(os.path.join(d, n), 'rb').read()

# Pares (bytes pares, bytes impares) -> offset, según ROM_START( sf2 ) de MAME.
PAIRS = [
    (0x00000, "sf2e.30g",    "sf2e.37g"),
    (0x40000, "sf2e.31g",    "sf2e.38g"),
    (0x80000, "sf2e.28g",    "sf2e.35g"),
    (0xC0000, "sf2_29a.bin", "sf2_36a.bin"),
]
prog = bytearray(0x100000)
for off, ev, od in PAIRS:
    e, o = rd(ev), rd(od)
    assert len(e) == len(o) == 0x20000, (ev, od)
    prog[off:off + 0x40000:2] = e
    prog[off + 1:off + 0x40000:2] = o
print("tamaño del programa: 0x%X bytes" % len(prog))

sp, pc = struct.unpack(">II", prog[:8])
print("SSP inicial=0x%08X  PC de reset=0x%08X" % (sp, pc))
for lvl in range(1, 8):
    print("  vector IRQ%d -> 0x%08X" % (lvl, struct.unpack(">I", prog[0x60 + lvl * 4:0x64 + lvl * 4])[0]))

# Conteo aproximado por barrido lineal (los datos meten falsos positivos).
cnt = {"jsr abs.l": 0, "jmp abs.l": 0, "jsr (an)": 0, "jmp (an)": 0,
       "jmp (d8,pc,xn)": 0, "jsr (d8,pc,xn)": 0, "jsr/jmp -> RAM(FFxxxx)": 0,
       "jsr/jmp abs.l -> 0xC0000-0xFFFFF": 0, "rts": 0}
for i in range(0, len(prog) - 6, 2):
    op = (prog[i] << 8) | prog[i + 1]
    if op == 0x4EB9: cnt["jsr abs.l"] += 1
    if op == 0x4EF9: cnt["jmp abs.l"] += 1
    if 0x4E90 <= op <= 0x4E97: cnt["jsr (an)"] += 1
    if 0x4ED0 <= op <= 0x4ED7: cnt["jmp (an)"] += 1
    if op == 0x4EFB: cnt["jmp (d8,pc,xn)"] += 1
    if op == 0x4EBB: cnt["jsr (d8,pc,xn)"] += 1
    if op in (0x4EB9, 0x4EF9):
        tgt = struct.unpack(">I", prog[i + 2:i + 6])[0]
        if (tgt >> 16) == 0x00FF: cnt["jsr/jmp -> RAM(FFxxxx)"] += 1
        if 0xC0000 <= tgt < 0x100000: cnt["jsr/jmp abs.l -> 0xC0000-0xFFFFF"] += 1
    if op == 0x4E75: cnt["rts"] += 1
print("conteos aproximados:", cnt)
for off in range(0, len(prog), 0x40000):
    seg = prog[off:off + 0x40000]
    ff = sum(1 for b in seg if b == 0xFF)
    rts = sum(1 for i in range(0, len(seg) - 1, 2) if seg[i] == 0x4E and seg[i + 1] == 0x75)
    print("segmento 0x%05X: %.1f%% bytes 0xFF, %d rts" % (off, 100 * ff / len(seg), rts))
print("sha1 de la imagen intercalada:", hashlib.sha1(prog).hexdigest())
if len(sys.argv) > 2:
    open(sys.argv[2], 'wb').write(prog)
