#!/usr/bin/env python3
"""compare_sound_cmds.py - compara el flujo de comandos de sonido del 68000 entre el runtime y MAME (hito 4).

Cada archivo (build/sf2_blind --sound-cmds, tools/mame_sound_cmds.lua) tiene una línea por escritura del 68000
en un latch de sonido, "cuadro latch valor" (valor en hexadecimal), y una última línea "# cuadros N" con el
punto de muestra hasta el que llegó la partida.  Se comparan, en orden, las escrituras de los cuadros
anteriores a min(N_a, N_b): los comandos del último cuadro de un lado pueden estar incompletos porque cada
lado para en un instante distinto de ese cuadro.

Dos listas son iguales si tienen las mismas escrituras (latch, valor y cuadro) en el mismo orden.  Si no, se
imprime la primera diferencia con las escrituras que la rodean en los dos lados.

  python3 tools/compare_sound_cmds.py build/audio/cmds_recomp.txt build/audio/cmds_mame.txt [--inputs tools/inputs_sf2.txt]

Código de salida: 0 si coinciden, 1 si hay diferencias.
"""
import argparse
import sys
from collections import Counter

# tramos de la partida de tools/inputs_sf2.txt (cuadro de inicio, nombre), los mismos que usa compare_audio.py
SECTIONS = [(0, "arranque y test de RAM"), (1, "aviso y atract"), (600, "moneda"), (1000, "start y selección"),
            (1500, "combate"), (3702, "KO, continuar, fin de partida"), (5000, "atract y demo")]


def load(path):
    cmds, frames = [], None
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                p = line[1:].split()
                if len(p) == 2 and p[0] == "cuadros":
                    frames = int(p[1])
                continue
            p = line.split()
            if len(p) != 3:
                sys.exit(f"{path}:{n}: formato esperado 'cuadro latch valor'")
            cmds.append((int(p[0]), int(p[1]), int(p[2], 16)))
    return cmds, frames


def fmt(c):
    return "(ninguno)" if c is None else f"cuadro {c[0]:5d} latch{c[1]} = {c[2]:02x}"


def section_of(frame):
    name = SECTIONS[0][1]
    for f0, label in SECTIONS:
        if frame >= f0:
            name = label
    return name


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("recomp", help="lista del runtime (build/sf2_blind --sound-cmds)")
    ap.add_argument("mame", help="lista de MAME (tools/mame_sound_cmds.lua)")
    ap.add_argument("--name-a", default="recomp")
    ap.add_argument("--name-b", default="mame")
    ap.add_argument("--context", type=int, default=3, help="escrituras que se muestran a cada lado de la diferencia")
    args = ap.parse_args()
    na, nb = args.name_a, args.name_b

    a, fa = load(args.recomp)
    b, fb = load(args.mame)
    limit = min(x for x in (fa, fb) if x is not None) if (fa is not None or fb is not None) else None
    print(f"{na}: {len(a)} escrituras, {fa} cuadros; {nb}: {len(b)} escrituras, {fb} cuadros")
    if limit is not None:
        a = [c for c in a if c[0] < limit]
        b = [c for c in b if c[0] < limit]
        print(f"se comparan los cuadros anteriores al {limit}: {na} {len(a)}, {nb} {len(b)}")

    first = None
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            first = i
            break
    if first is None and len(a) != len(b):
        first = min(len(a), len(b))

    # resumen del flujo (sirve también para ver qué recorre la partida)
    ref = b if b else a
    by_latch = Counter(c[1] for c in ref)
    print(f"flujo ({nb}): latch 1 (comandos) {by_latch.get(1, 0)}, latch 2 (fade) {by_latch.get(2, 0)}; "
          f"{len(set(c[2] for c in ref if c[1] == 1))} valores distintos en el latch 1; "
          f"{len(set(c[0] for c in ref))} cuadros con comandos")
    per = Counter(section_of(c[0]) for c in ref)
    for _, label in SECTIONS:
        if per.get(label):
            print(f"    {label:32s} {per[label]:5d} escrituras")

    if first is None:
        print(f"RESULTADO: las {len(a)} escrituras coinciden en latch, valor y cuadro")
        return 0

    print(f"RESULTADO: primera diferencia en la escritura {first}")
    lo, hi = max(0, first - args.context), first + args.context + 1
    for i in range(lo, hi):
        ca = a[i] if i < len(a) else None
        cb = b[i] if i < len(b) else None
        if ca is None and cb is None:
            break
        mark = ">>" if i == first else "  "
        print(f"  {mark} #{i:<6d} {na}: {fmt(ca):34s} {nb}: {fmt(cb)}")
    la, lb = len(a), len(b)
    if la != lb:
        print(f"  número distinto de escrituras: {na} {la}, {nb} {lb}")
    else:
        bad = sum(1 for x, y in zip(a, b) if x != y)
        print(f"  {bad} de {la} escrituras distintas en total")
    return 1


if __name__ == "__main__":
    sys.exit(main())
