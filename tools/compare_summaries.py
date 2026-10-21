#!/usr/bin/env python3
"""Compara los resúmenes por cuadro del runtime (build/sf2_blind) y de MAME (tools/mame_blind.lua).

Cada línea: f=N pc=.. sr=.. d=.. a=.. in=.. fc=.. w=.. v=.. g=.. [wb=.. gb=..]

Veredicto por cuadro:
  - DIFERENCIA   si w (RAM de trabajo sin zonas volátiles), g (RAM gráfica), in (entradas leídas) o fc
                 (contador de cuadros del juego) no coinciden
  - info         si solo difieren pc/sr/registros o v (zonas volátiles): la IRQ interrumpió el bucle de
                 espera en otro punto; se cuenta pero no es un fallo

Veredicto global (código de salida):
  0  sin diferencias, o solo rachas aisladas de hasta --max-run cuadros (3) seguidas de
     resincronización: la IRQ cortó una tarea larga en otra instrucción (relojes distintos) y el
     estado converge en el cuadro siguiente; se listan igualmente
  1  alguna racha más larga, o los resúmenes no vuelven a coincidir antes del final (divergencia real),
     o el campo de entradas (in=) difiere en algún cuadro: eso nunca es cuestión de temporización.
     Con --inputs tabla se indica además si en ese cuadro había inyección programada (desalineación
     entre los dos lados) o no (entrada ajena a la tabla, p. ej. un teclado o mando del anfitrión
     leído por MAME).
  --strict: cualquier diferencia devuelve 1

Uso:
  compare_summaries.py recomp.txt mame.txt [--inputs tabla] [--context N] [--all] [--max-diffs N] [--max-run N] [--strict]
  compare_summaries.py --ram a.bin b.bin [--base 0xff0000]      (compara dos volcados de RAM)
"""
import argparse
import sys


def parse(path):
    rows = {}
    order = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith("f="):
                continue
            d = {}
            for tok in line.split():
                k, _, v = tok.partition("=")
                d[k] = v
            n = int(d["f"])
            rows[n] = d
            order.append(n)
    return rows, order


def diff_fields(a, b):
    hard, soft = [], []
    for k in ("w", "g", "in", "fc"):
        if a.get(k) != b.get(k):
            hard.append(k)
    for k in ("pc", "sr", "d", "a", "v"):
        if a.get(k) != b.get(k):
            soft.append(k)
    return hard, soft


def block_detail(a, b):
    out = []
    for key, size, base in (("wb", 0x1000, 0xFF0000), ("gb", 0x4000, 0x900000)):
        if key in a and key in b:
            xa, xb = a[key].split(","), b[key].split(",")
            for i, (p, q) in enumerate(zip(xa, xb)):
                if p != q:
                    out.append("%s bloque %d (0x%06X-0x%06X)" % (key, i, base + i * size, base + (i + 1) * size - 1))
    return out


def regs_detail(a, b):
    out = []
    for key, names in (("d", ["d%d" % i for i in range(8)]), ("a", ["a%d" % i for i in range(8)])):
        if a.get(key) != b.get(key):
            xa, xb = a[key].split(","), b[key].split(",")
            for n, p, q in zip(names, xa, xb):
                if p != q:
                    out.append("%s %s/%s" % (n, p, q))
    return out


def load_inputs(path):
    """Devuelve la lista de (desde, hasta, tecla) de la tabla de entradas, o None si no hay tabla."""
    if not path:
        return None
    events = []
    with open(path) as f:
        for line in f:
            body = line.split("#", 1)[0].split()
            if len(body) >= 3 and body[0].isdigit() and body[1].isdigit():
                events.append((int(body[0]), int(body[1]), body[2]))
    return events


def scheduled_inputs(events, frame):
    """Teclas programadas en el cuadro `frame` o en los contiguos (un desfase de un cuadro también cuenta)."""
    return [k for a, b, k in events if a - 1 <= frame <= b + 1]


def compare(path_a, path_b, context, show_all, max_diffs, max_run=3, strict=False, inputs_path=None):
    ra, oa = parse(path_a)
    rb, ob = parse(path_b)
    events = load_inputs(inputs_path)
    common = sorted(set(ra) & set(rb))
    print("%s: %d cuadros (%d..%d)" % (path_a, len(ra), min(oa) if oa else 0, max(oa) if oa else 0))
    print("%s: %d cuadros (%d..%d)" % (path_b, len(rb), min(ob) if ob else 0, max(ob) if ob else 0))
    if not common:
        print("sin cuadros en común")
        return 2
    hard_frames, soft_frames, input_frames = [], [], []
    first_hard = None
    for n in common:
        hard, soft = diff_fields(ra[n], rb[n])
        if hard:
            hard_frames.append((n, hard))
            if first_hard is None:
                first_hard = n
            if "in" in hard:
                input_frames.append(n)
        if soft:
            soft_frames.append((n, soft))
    print("cuadros comparados: %d (%d..%d)" % (len(common), common[0], common[-1]))
    if input_frames:
        print("DIFERENCIA DE ENTRADAS en %d cuadros (primero %d): el juego leyo entradas distintas en cada lado" % (
            len(input_frames), input_frames[0]))
        for n in input_frames[:10]:
            sched = scheduled_inputs(events, n) if events is not None else None
            if sched is None:
                why = "(sin tabla de entradas: use --inputs para saber si estaba programada)"
            elif sched:
                why = "con inyeccion programada de %s: desalineacion entre los dos lados" % ",".join(sorted(set(sched)))
            else:
                why = "SIN inyeccion programada: entrada ajena a la tabla (teclado o mando del anfitrion leido por MAME, o fallo del runtime)"
            print("  cuadro %d: %s in=%s / %s in=%s %s" % (n, path_a.split("/")[-1], ra[n]["in"], path_b.split("/")[-1], rb[n]["in"], why))
        if len(input_frames) > 10:
            print("  ... %d cuadros mas con entradas distintas" % (len(input_frames) - 10))
    print("cuadros con diferencia en registros/pila (info): %d" % len(soft_frames))
    # histograma de pares de PC interrumpidos cuando difieren
    pcs = {}
    for n, soft in soft_frames:
        if "pc" in soft:
            key = (ra[n]["pc"], rb[n]["pc"])
            pcs[key] = pcs.get(key, 0) + 1
    if pcs:
        top = sorted(pcs.items(), key=lambda kv: -kv[1])[:8]
        print("  pares de pc interrumpido más frecuentes (%s / %s): %s" % (
            path_a.split("/")[-1], path_b.split("/")[-1], ", ".join("%s/%s x%d" % (k[0], k[1], v) for k, v in top)))
    if not hard_frames:
        print("OK: %d cuadros con la misma RAM de trabajo, RAM gráfica, entradas y contador de cuadros" % len(common))
        return 0
    print("DIFERENCIA: %d cuadros con RAM/entradas distintas; primero en el cuadro %d (%s)" % (
        len(hard_frames), first_hard, ",".join(hard_frames[0][1])))
    # rachas de diferencias
    runs = []
    for n, hard in hard_frames:
        if runs and n == runs[-1][1] + 1:
            runs[-1][1] = n
        else:
            runs.append([n, n])
    print("rachas de cuadros distintos: %s%s" % (
        ", ".join("%d-%d" % (a, b) if a != b else "%d" % a for a, b in runs[:20]), " ..." if len(runs) > 20 else ""))
    longest = max(b - a + 1 for a, b in runs)
    resynced = runs[-1][1] < common[-1]
    if resynced:
        print("vuelven a coincidir desde el cuadro %d hasta el final (%d)" % (runs[-1][1] + 1, common[-1]))
    else:
        print("no vuelven a coincidir antes del final (%d)" % common[-1])
    isolated = resynced and longest <= max_run and not strict and not input_frames
    verdict = 0 if isolated else 1
    if isolated:
        print("VEREDICTO: aceptable, %d rachas aisladas de como mucho %d cuadros, todas seguidas de "
              "resincronizacion (la IRQ corto una tarea larga en otro punto)" % (len(runs), longest))
    elif input_frames:
        print("VEREDICTO: divergencia por entradas distintas (ver arriba); las rachas de RAM posteriores derivan de ella")
    else:
        print("VEREDICTO: divergencia (racha mas larga %d cuadros%s)" % (longest, "" if resynced else ", sin resincronizar"))
    shown = 0
    for n, hard in hard_frames:
        if shown >= max_diffs and not show_all:
            print("... (%d cuadros distintos más; --all para verlos)" % (len(hard_frames) - shown))
            break
        shown += 1
        print("\ncuadro %d: difieren %s" % (n, ",".join(hard)))
        for k in range(max(common[0], n - context), min(common[-1], n + context) + 1):
            if k in ra and k in rb:
                ha, _ = diff_fields(ra[k], rb[k])
                mark = "*" if ha else " "
                print(" %s %s" % (mark, fmt_row(ra[k], path_a)))
                print(" %s %s" % (mark, fmt_row(rb[k], path_b)))
        det = block_detail(ra[n], rb[n])
        if det:
            print("  bloques distintos: " + "; ".join(det))
        rd = regs_detail(ra[n], rb[n])
        if rd:
            print("  registros distintos: " + ", ".join(rd))
    return verdict


def fmt_row(d, path):
    tag = path.split("/")[-1].replace("summary_", "").replace(".txt", "")
    return "%-7s f=%s pc=%s sr=%s in=%s fc=%s w=%s v=%s g=%s" % (tag, d["f"], d["pc"], d["sr"], d["in"], d["fc"], d["w"], d["v"], d["g"])


def compare_ram(path_a, path_b, base):
    a = open(path_a, "rb").read()
    b = open(path_b, "rb").read()
    if len(a) != len(b):
        print("tamaños distintos: %d y %d" % (len(a), len(b)))
    n = min(len(a), len(b))
    ranges = []
    start = None
    for i in range(n):
        if a[i] != b[i]:
            if start is None:
                start = i
        elif start is not None:
            ranges.append((start, i - 1))
            start = None
    if start is not None:
        ranges.append((start, n - 1))
    total = sum(e - s + 1 for s, e in ranges)
    print("%d bytes distintos en %d rangos" % (total, len(ranges)))
    for s, e in ranges[:200]:
        ha = a[s:e + 1][:16].hex()
        hb = b[s:e + 1][:16].hex()
        print("  0x%06X-0x%06X (%d bytes): %s%s / %s%s" % (base + s, base + e, e - s + 1, ha, "..." if e - s + 1 > 16 else "",
                                                           hb, "..." if e - s + 1 > 16 else ""))
    if len(ranges) > 200:
        print("  ... %d rangos más" % (len(ranges) - 200))
    return 1 if ranges else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--context", type=int, default=2)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--max-diffs", type=int, default=5)
    ap.add_argument("--max-run", type=int, default=3, help="racha máxima de cuadros distintos que se acepta como aislada")
    ap.add_argument("--strict", action="store_true", help="cualquier diferencia devuelve 1")
    ap.add_argument("--inputs", help="tabla de entradas usada en ambos lados, para clasificar las diferencias de in=")
    ap.add_argument("--ram", action="store_true", help="a y b son volcados binarios de RAM")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0xFF0000)
    args = ap.parse_args()
    if args.ram:
        sys.exit(compare_ram(args.a, args.b, args.base))
    sys.exit(compare(args.a, args.b, args.context, args.all, args.max_diffs, args.max_run, args.strict, args.inputs))


if __name__ == "__main__":
    main()
