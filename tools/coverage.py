#!/usr/bin/env python3
"""coverage.py - corre a ciegas todas las secuencias de tools/sequences/ y escribe el informe de cobertura (hito 6).

Para cada secuencia (formato en tools/sequences/README.md) lanza build/sf2_blind sin red de seguridad de Musashi, con
sus dipswitches y su número de cuadros, y recoge: el resumen por cuadro (para compararlo con MAME), las direcciones
de instrucción ejecutadas (--cov) y cada despacho por la tabla (--dispatch-log).  Después cruza todo con el análisis
estático (tools/analyze.py con tools/hints.txt) y escribe un informe en Markdown:

  - instrucciones y funciones ejecutadas frente a las estáticas, por secuencia y en total;
  - despachos por la tabla (saltos indirectos, retornos no estructurados, excepciones, trampolín) clasificados según
    el análisis los hubiera enumerado o no para esa instrucción de origen, y la lista de los destinos que el análisis
    no había enumerado para ese origen (llegaron a la tabla por otro motivo);
  - fallos de despacho (direcciones fuera de la tabla): deben ser 0.

  python3 tools/coverage.py --blind build/sf2_blind --rom build/rom.bin --out build/coverage [--jobs N] [--only nombre]
  python3 tools/coverage.py --report-only --out build/coverage            # rehace el informe con lo ya corrido

Sale con 1 si alguna secuencia termina con error (fallo de despacho u otro) o no reproduce el estado final que anotó
el generador (#@ state_crc no se comprueba aquí: lo comprueba tools/sequences.py --verify con la biblioteca).
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

IRQ2_HANDLER = 0x000AB8
DEFAULT_DIPS = "ff,fc,9f"


def read_header(path):
    """directivas '#@ clave valor' de una secuencia"""
    h = {"frames": None, "dips": DEFAULT_DIPS, "title": os.path.basename(path), "snap": "", "marks": []}
    first_comment = None
    with open(path) as f:
        for line in f:
            if line.startswith("#@"):
                parts = line[2:].strip().split(None, 1)
                if not parts:
                    continue
                k, v = parts[0], (parts[1].strip() if len(parts) > 1 else "")
                if k == "dips":
                    v = ",".join(v.replace(",", " ").split())
                if k == "mark":
                    h["marks"].append(int(v.split()[0]))
                    continue
                h[k] = v
            elif line.startswith("#") and first_comment is None:
                first_comment = line[1:].strip()
    if first_comment:
        h["title"] = first_comment
    if h["frames"] is None:
        raise SystemExit("%s: falta la directiva '#@ frames N'" % path)
    h["frames"] = int(h["frames"])
    return h


def sequences(seq_dir, only=None):
    out = []
    for name in sorted(os.listdir(seq_dir)):
        if not name.endswith(".txt"):
            continue
        key = name[:-4]
        if only and key not in only:
            continue
        out.append((key, os.path.join(seq_dir, name)))
    return out


def run_one(blind, rom, key, path, outdir, extra=()):
    h = read_header(path)
    d = os.path.join(outdir, key)
    os.makedirs(d, exist_ok=True)
    cmd = [blind, rom, "--frames", str(h["frames"]), "--inputs", path, "--dips", h["dips"], "--quiet",
           "--summary", os.path.join(d, "summary_recomp.txt"), "--cov", os.path.join(d, "cov.txt"),
           "--dispatch-log", os.path.join(d, "dispatch.txt"), "--sound-cmds", os.path.join(d, "cmds_recomp.txt")]
    cmd += list(extra)
    t0 = time.time()
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    dt = time.time() - t0
    with open(os.path.join(d, "blind.log"), "w") as f:
        f.write(" ".join(cmd) + "\n" + p.stdout)
    return key, p.returncode, dt, p.stdout


def parse_log(text):
    r = {}
    m = re.search(r"^(OK|limite de instrucciones): (\d+) cuadros, (\d+) instrucciones", text, re.M)
    if m:
        r["frames"], r["insns"] = int(m.group(2)), int(m.group(3))
    m = re.search(r"cobertura dinamica: (\d+) instrucciones distintas", text)
    if m:
        r["distinct"] = int(m.group(1))
    m = re.search(r"despachos por la tabla: (\d+) \((\d+) pares", text)
    if m:
        r["dispatches"], r["pairs"] = int(m.group(1)), int(m.group(2))
    m = re.search(r"ERROR FATAL.*", text)
    if m:
        r["error"] = m.group(0)
    return r


def load_cov(path):
    s = set()
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line:
                    s.add(int(line, 16))
    return s


def load_dispatch(path):
    pairs = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                a, b, n, k = line.split()
                key = (int(a, 16), int(b, 16))
                c, _ = pairs.get(key, (0, 1))
                pairs[key] = (c + int(n), int(k))
    return pairs


# ---- clasificación de los despachos ---------------------------------------------------------------------------
def classify(an, frm, to, known, ret_points, code_ptrs, trap_handlers):
    """(categoría, enumerado) de un despacho de `frm` a `to`"""
    if not known:
        return "fuera de la tabla (fallo)", False
    if frm == to:
        return "reanudación tras ceder (machine_step)", True
    if frm == 0:
        return "arranque (vector de reset)", True
    if to == IRQ2_HANDLER:
        return "interrupción IRQ2", True
    ins = an.insns.get(frm)
    if ins is None:
        return "origen fuera del análisis", False
    m = ins.mnem
    if m == "trap" or m in ("chk", "divu", "divs", "illegal", "trapv"):
        return "excepción (%s)" % m, to in trap_handlers or to in an.calls.get(frm, ())
    if m in ("jmp", "jsr"):
        direct = ins.ops[0].abs_addr() is not None
        ind = an.indirects.get(frm)
        if not direct and ind is not None and not ind.resolved:
            # jmp (An) sin destinos estáticos: retorno por registro (destinos lea-ret) o cambio de tarea del planificador
            # (0xAB6: PC de tareas creadas con trap, destinos trap-a0/a1/tabla)
            return "salto por registro (retorno por registro o cambio de tarea)", to in ret_points or to in code_ptrs
        kind = ("llamada directa a mitad de función" if direct else "llamada indirecta") if m == "jsr" else \
               ("salto directo a mitad de función" if direct else "salto indirecto")
        return kind, to in an.jumps.get(frm, ()) or to in an.calls.get(frm, ()) or (direct and (ins.ops[0].abs_addr() & 0xFFFFFF) == to)
    if m in ("bsr", "bra", "bcc"):
        return "rama directa a otra función", True
    if m in ("rts", "rtr"):
        return "retorno no estructurado (%s)" % m, to in ret_points or to in code_ptrs
    if m == "rte":
        return "rte a otra tarea (trampolín)", to in ret_points or to in code_ptrs or to in an.entries
    return "otro (%s)" % m, to in an.labels


def report(outdir, keys, logs, an, out_md):
    """escribe el informe; devuelve el número de problemas (secuencias con error, fallos de despacho)"""
    static_insns = set(an.insns)
    funcs = set(an.funcs)
    ret_points = set()
    for a, ins in an.insns.items():
        if ins.mnem in ("bsr", "jsr", "trap"):
            ret_points.add(ins.next)
    ret_points |= {a for a, s in an.label_src.items() if s in ("ret", "lea-ret")}
    code_ptrs = set(an.code_ptrs) | {a for a, s in an.entry_src.items() if s in ("pea", "lea-ret") or s.startswith("trap")}
    trap_handlers = {a for a, s in an.entry_src.items() if s in ("trap",) or s.startswith("vector")}

    total_cov = set()
    total_disp = {}
    rows = []
    problems = 0
    base_cov = None
    for key in keys:
        d = os.path.join(outdir, key)
        cov = load_cov(os.path.join(d, "cov.txt"))
        disp = load_dispatch(os.path.join(d, "dispatch.txt"))
        lg = logs.get(key) or parse_log(open(os.path.join(d, "blind.log")).read() if os.path.exists(os.path.join(d, "blind.log")) else "")
        if key == "validacion":
            base_cov = cov
        new = len(cov - total_cov)
        total_cov |= cov
        for k, (n, kn) in disp.items():
            c, _ = total_disp.get(k, (0, kn))
            total_disp[k] = (c + n, kn)
        fx = len(cov & funcs)
        err = lg.get("error", "")
        if err or not lg.get("frames"):
            problems += 1
        misses = sum(n for (a, b), (n, kn) in disp.items() if not kn)
        problems += 1 if misses else 0
        rows.append((key, lg.get("frames", 0), lg.get("insns", 0), len(cov), new, fx, len(disp), misses, err))

    cats = {}
    not_enum = {}
    for (frm, to), (n, kn) in total_disp.items():
        cat, enum = classify(an, frm, to, kn, ret_points, code_ptrs, trap_handlers)
        c = cats.setdefault(cat, [0, 0, 0, 0])      # pares, veces, pares no enumerados, veces no enumeradas
        c[0] += 1
        c[1] += n
        if not enum:
            c[2] += 1
            c[3] += n
            not_enum.setdefault(to, []).append((frm, cat, n, an.label_src.get(to, "-")))

    L = []
    L.append("# Informe de cobertura dinámica (hito 6)\n")
    L.append("Generado por `tools/coverage.py` (`make coverage`). Cada secuencia de `tools/sequences/` corre a ciegas en "
             "`sf2_blind` **sin red de seguridad de Musashi**: una dirección fuera de la tabla de despacho detiene la "
             "partida con un error.\n")
    L.append("## Por secuencia\n")
    L.append("| Secuencia | Cuadros | Instrucciones ejecutadas | Distintas | Nuevas (acumulado) | Funciones con la entrada ejecutada | Pares de despacho | Fallos de despacho | Error |")
    L.append("|---|---:|---:|---:|---:|---:|---:|---:|---|")
    for key, fr, ni, dist, new, fx, npairs, misses, err in rows:
        L.append("| %s | %d | %s | %d (%.1f %%) | %d | %d (%.1f %%) | %d | %d | %s |" % (
            key, fr, format(ni, ","), dist, 100.0 * dist / len(static_insns), new, fx, 100.0 * fx / len(funcs), npairs,
            misses, err.replace("|", "/") if err else "—"))
    L.append("")
    tot_f = len(total_cov & funcs)
    L.append("## Total\n")
    L.append("- Instrucciones estáticas (análisis con `tools/hints.txt`): **%d**; ejecutadas por alguna secuencia: **%d (%.1f %%)**." % (
        len(static_insns), len(total_cov & static_insns), 100.0 * len(total_cov & static_insns) / len(static_insns)))
    if base_cov is not None:
        L.append("- Solo la partida de validación de los hitos 2-5 (`validacion`): %d (%.1f %%); las demás secuencias añaden %d." % (
            len(base_cov), 100.0 * len(base_cov) / len(static_insns), len(total_cov - base_cov)))
    L.append("- Funciones (entradas del análisis): **%d**; con la entrada ejecutada: **%d (%.1f %%)**." % (
        len(funcs), tot_f, 100.0 * tot_f / len(funcs)))
    outside = total_cov - static_insns
    L.append("- Instrucciones ejecutadas que el análisis no conoce: **%d** (deben ser 0: sin red de seguridad no se pueden ejecutar)." % len(outside))
    und = an.undecoded_successors()
    L.append("- Conflictos del análisis (destinos de rama o de caída que no son una instrucción decodificada): **%d**%s." % (
        len(und), (": " + ", ".join("%06x" % t for t in sorted(und))) if und else ""))
    labels_hit = {to for (frm, to) in total_disp} & set(an.labels)
    L.append("- Etiquetas de la tabla de despacho: %d; alcanzadas por despacho: %d." % (len(an.labels), len(labels_hit)))
    L.append("")
    L.append("## Despachos por la tabla\n")
    L.append("| Tipo | Pares origen-destino | Veces | Pares no enumerados por el análisis | Veces |")
    L.append("|---|---:|---:|---:|---:|")
    for cat in sorted(cats, key=lambda c: -cats[c][1]):
        c = cats[cat]
        L.append("| %s | %d | %s | %d | %s |" % (cat, c[0], format(c[1], ","), c[2], format(c[3], ",")))
    L.append("")
    L.append("## Destinos alcanzados por la tabla de despacho que el análisis no había enumerado para ese origen\n")
    if not not_enum:
        L.append("Ninguno: todo destino despachado estaba previsto por el análisis para su instrucción de origen.\n")
    else:
        L.append("El destino estaba en la tabla (por eso la partida siguió) pero no como destino de esa instrucción: llegó a la "
                 "tabla por otro motivo (columna «motivo de la etiqueta» del análisis).\n")
        L.append("| Destino | Motivo de la etiqueta | Orígenes (instrucción, tipo, veces) |")
        L.append("|---|---|---|")
        for to in sorted(not_enum):
            srcs = "; ".join("%06x %s ×%d" % (frm, cat, n) for frm, cat, n, _ in sorted(not_enum[to])[:6])
            more = len(not_enum[to]) - 6
            if more > 0:
                srcs += "; … y %d más" % more
            L.append("| %06x | %s | %s |" % (to, not_enum[to][0][3], srcs))
        L.append("")
    # tramos de instrucciones consecutivas (en el orden de las direcciones) que ninguna secuencia ejecutó
    regions = []
    cur = None
    for a in sorted(static_insns):
        if a in total_cov:
            cur = None
            continue
        if cur is None:
            cur = [a, a, 0, 0]
            regions.append(cur)
        cur[1] = a
        cur[2] += 1
        cur[3] += 1 if a in funcs else 0
    L.append("## Código no ejecutado\n")
    L.append("%d instrucciones en %d tramos (instrucciones consecutivas sin ninguna ejecutada entre ellas). Los 25 mayores, con "
             "el motivo por el que el análisis llegó a la primera función del tramo:\n" % (
                 sum(r[2] for r in regions), len(regions)))
    L.append("| Desde | Hasta | Instrucciones | Funciones que empiezan en el tramo | Motivo de la primera |")
    L.append("|---|---|---:|---:|---|")
    for r in sorted(regions, key=lambda r: -r[2])[:25]:
        first = next((a for a in sorted(funcs) if r[0] <= a <= r[1]), None)
        L.append("| %06x | %06x | %d | %d | %s |" % (r[0], r[1], r[2], r[3],
                                                    an.entry_src.get(first, "-") if first is not None else "(dentro de una función ejecutada)"))
    L.append("")
    misses = [(k, v) for k, v in total_disp.items() if not v[1]]
    L.append("## Fallos de despacho (direcciones fuera de la tabla)\n")
    if misses:
        for (frm, to), (n, _) in sorted(misses):
            L.append("- salto a %06x desde %06x (%d veces): añadir `entry %06x` a `tools/hints.txt`" % (to, frm, n, to))
    else:
        L.append("Ninguno.")
    L.append("")
    with open(out_md, "w") as f:
        f.write("\n".join(L) + "\n")
    return problems, len(total_cov & static_insns), len(static_insns), tot_f, len(funcs)


# ---- validación contra MAME ------------------------------------------------------------------------------------
MAME_HEADLESS = ["-video", "none", "-sound", "none", "-nothrottle", "-keyboardprovider", "none", "-mouseprovider", "none",
                 "-joystickprovider", "none", "-lightgunprovider", "none"]


def snap_frames(h, every):
    """cuadros de vídeo que se comparan: los de '#@ snap' y uno cada `every`"""
    fr = set(int(x) for x in h.get("snap", "").replace(",", " ").split() if x)
    fr |= set(range(every, h["frames"] + 1, every))
    return sorted(f for f in fr if 1 <= f <= h["frames"])


def mame_one(key, path, outdir, blind, rom, roms, every):
    """MAME (resumen, comandos de sonido y capturas) y el runtime (capturas) para una secuencia; luego compara"""
    h = read_header(path)
    d = os.path.join(outdir, key)
    os.makedirs(d, exist_ok=True)
    cfg = os.path.join(d, "mame_cfg")
    os.makedirs(cfg, exist_ok=True)
    for f in os.listdir(cfg):                   # cfg propio y vacío: MAME guarda en él los dipswitches al salir
        if f.endswith(".cfg"):
            os.remove(os.path.join(cfg, f))
    snaps = snap_frames(h, every)
    snapdir = os.path.join(d, "mame_png")
    os.makedirs(snapdir, exist_ok=True)
    mame = os.path.join(ROOT, "tools", "mame.sh")
    lua = os.path.join(ROOT, "tools", "mame_blind.lua")
    log = open(os.path.join(d, "mame.log"), "w")
    t0 = time.time()
    env = dict(os.environ, SF2_INPUTS=path, SF2_DIPS=h["dips"])
    if h["dips"] != DEFAULT_DIPS:
        # pasada corta: deja los dipswitches en el cfg propio para que valgan desde el encendido (ver mame_blind.lua)
        e1 = dict(env, SF2_SUMMARY=os.path.join(d, "summary_mame_prep.txt"), SF2_FRAMES="1")
        subprocess.run([mame, "sf2"] + MAME_HEADLESS + ["-cfg_directory", cfg, "-str", "30", "-autoboot_script", lua],
                       env=e1, stdout=log, stderr=subprocess.STDOUT)
    e2 = dict(env, SF2_SUMMARY=os.path.join(d, "summary_mame.txt"), SF2_FRAMES=str(h["frames"]),
              SF2_CMDS=os.path.join(d, "cmds_mame.txt"), SF2_SNAP_FRAMES=",".join(str(f) for f in snaps))
    rc = subprocess.run([mame, "sf2"] + MAME_HEADLESS + ["-cfg_directory", cfg, "-snapshot_directory", snapdir,
                                                         "-str", str(h["frames"] // 55 + 30), "-autoboot_script", lua],
                        env=e2, stdout=log, stderr=subprocess.STDOUT).returncode
    t_mame = time.time() - t0
    # capturas del runtime en los mismos cuadros
    pngdir = os.path.join(d, "png")
    os.makedirs(pngdir, exist_ok=True)
    subprocess.run([blind, rom, "--frames", str(snaps[-1] + 1 if snaps else 1), "--inputs", path, "--dips", h["dips"],
                    "--gfx", roms, "--png-frames", ",".join(str(f) for f in snaps), "--png-dir", pngdir, "--quiet"],
                   stdout=log, stderr=subprocess.STDOUT)
    log.close()
    res = {"key": key, "mame_rc": rc, "t_mame": t_mame, "frames": h["frames"], "snaps": len(snaps)}
    with open(os.path.join(d, "mame_time.txt"), "w") as f:
        f.write("%.1f\n" % t_mame)
    py = sys.executable
    tools = os.path.join(ROOT, "tools")
    p = subprocess.run([py, os.path.join(tools, "compare_summaries.py"), os.path.join(d, "summary_recomp.txt"),
                        os.path.join(d, "summary_mame.txt"), "--inputs", path], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
    res["summary_rc"], res["summary_out"] = p.returncode, p.stdout
    p = subprocess.run([py, os.path.join(tools, "compare_sound_cmds.py"), os.path.join(d, "cmds_recomp.txt"),
                        os.path.join(d, "cmds_mame.txt")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    res["cmds_rc"], res["cmds_out"] = p.returncode, p.stdout
    p = subprocess.run([py, os.path.join(tools, "compare_frames.py"), "--dir-a", pngdir, "--dir-b", snapdir, "--frames",
                        ",".join(str(f) for f in snaps), "--diff-dir", os.path.join(d, "diff")],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    res["video_rc"], res["video_out"] = p.returncode, p.stdout
    for name in ("summary", "cmds", "video"):
        with open(os.path.join(d, "compare_%s.txt" % name), "w") as f:
            f.write(res[name + "_out"])
    return res


# ---- guardar y cargar a mitad de partida ------------------------------------------------------------------------
def roundtrip_points(h, n=4):
    """cuadros donde guardar/reiniciar/cargar: repartidos por la partida y justo después de cada hito anotado"""
    pts = set(h["frames"] * k // (n + 1) for k in range(1, n + 1))
    for line in h.get("marks", []):
        pts.add(line + 10)
    return sorted(p for p in pts if 10 < p < h["frames"] - 10)


def sha256_file(path):
    import hashlib
    d = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            d.update(chunk)
    return d.hexdigest()


def roundtrip_one(key, path, outdir, blind, rom, roms):
    """la partida seguida y la misma guardando, reiniciando y cargando el estado en varios puntos (con sonido):
    el resumen por cuadro y el WAV tienen que salir idénticos"""
    h = read_header(path)
    d = os.path.join(outdir, key)
    os.makedirs(d, exist_ok=True)
    pts = roundtrip_points(h)
    base = [blind, rom, "--frames", str(h["frames"]), "--inputs", path, "--dips", h["dips"], "--sound-roms", roms, "--quiet"]
    wa, wb = os.path.join(d, "rt_seguida.wav"), os.path.join(d, "rt_ida_vuelta.wav")
    t0 = time.time()
    pa = subprocess.run(base + ["--wav", wa, "--summary", os.path.join(d, "summary_seguida.txt")],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    pb = subprocess.run(base + ["--wav", wb, "--summary", os.path.join(d, "summary_ida_vuelta.txt"),
                                "--roundtrip", ",".join(str(p) for p in pts)],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    res = {"key": key, "points": len(pts), "rc": (pa.returncode, pb.returncode), "t": time.time() - t0,
           "log": pb.stdout}
    sa = sha256_file(os.path.join(d, "summary_seguida.txt"))
    sb = sha256_file(os.path.join(d, "summary_ida_vuelta.txt"))
    res["summary_same"] = sa == sb
    res["wav_same"] = os.path.exists(wa) and os.path.exists(wb) and sha256_file(wa) == sha256_file(wb)
    res["summary_sha"] = sa
    res["wav_bytes"] = os.path.getsize(wa) if os.path.exists(wa) else 0
    for w in (wa, wb):                     # los WAV de partidas enteras pesan cientos de MB: solo interesa su huella
        if os.path.exists(w):
            os.remove(w)
    with open(os.path.join(d, "roundtrip.log"), "w") as f:
        f.write(pa.stdout + "\n---\n" + pb.stdout)
    return res


def summary_digest(out):
    """(cuadros comparados, cuadros distintos, rachas) de la salida de compare_summaries.py"""
    m = re.search(r"DIFERENCIA: (\d+) cuadros", out)
    n = int(m.group(1)) if m else 0
    m = re.search(r"rachas de cuadros distintos: (.*)", out)
    runs = m.group(1).strip() if m else ""
    m = re.search(r"VEREDICTO: (.*)", out)
    verdict = m.group(1).strip() if m else ("identicos" if "IGUALES" in out or n == 0 else "?")
    return n, runs, verdict


def mame_results_from_disk(outdir, seqs, every):
    """reconstruye los resultados de mame_one a partir de los compare_*.txt de cada secuencia (para juntar tandas)"""
    results = []
    for key, path in seqs:
        d = os.path.join(outdir, key)
        if not os.path.exists(os.path.join(d, "compare_summary.txt")):
            continue
        h = read_header(path)
        r = {"key": key, "mame_rc": 0, "t_mame": 0.0, "frames": h["frames"], "snaps": len(snap_frames(h, every))}
        for name in ("summary", "cmds", "video"):
            r[name + "_out"] = open(os.path.join(d, "compare_%s.txt" % name)).read()
        so = r["summary_out"]
        r["summary_rc"] = 0 if ("OK:" in so or "VEREDICTO: aceptable" in so) else 1
        r["cmds_rc"] = 0 if "coinciden en latch, valor y cuadro" in r["cmds_out"] else 1
        vm = re.search(r"(\d+) de (\d+) cuadros identicos", r["video_out"])
        r["video_rc"] = 0 if vm and vm.group(1) == vm.group(2) else 1
        tf = os.path.join(d, "mame_time.txt")
        if os.path.exists(tf):
            r["t_mame"] = float(open(tf).read().strip() or 0)
        results.append(r)
    return results


def mame_report(results, out_md):
    L = ["# Validación de las secuencias contra MAME 0.289 (hito 6)\n",
         "Generado por `tools/coverage.py --mame` (`make compare-coverage`). Para cada secuencia: resumen por cuadro "
         "(`tools/compare_summaries.py`: RAM de trabajo sin zonas volátiles, RAM gráfica, entradas y contador de cuadros), "
         "comandos de sonido del 68000 (`tools/compare_sound_cmds.py`) y capturas de vídeo píxel a píxel "
         "(`tools/compare_frames.py`) en los cuadros de `#@ snap` y uno de cada N.\n",
         "| Secuencia | Cuadros | Distintos (resumen) | Rachas | Veredicto | Comandos de sonido | Vídeo | MAME (s) |",
         "|---|---:|---:|---|---|---|---|---:|"]
    bad = 0
    for r in sorted(results, key=lambda r: r["key"]):
        n, runs, verdict = summary_digest(r["summary_out"])
        cm = re.search(r"las (\d+) escrituras coinciden", r["cmds_out"])
        cmds = ("iguales (%s)" % cm.group(1)) if r["cmds_rc"] == 0 and cm else ("iguales" if r["cmds_rc"] == 0 else "DISTINTOS")
        vm = re.search(r"(\d+) de (\d+) cuadros identicos", r["video_out"])
        vi, vn = (int(vm.group(1)), int(vm.group(2))) if vm else (0, r["snaps"])
        video = ("%d de %d idénticos" % (vi, vn)) if r["video_rc"] == 0 else "DISTINTO (%d de %d idénticos)" % (vi, vn)
        ok = r["summary_rc"] == 0 and r["cmds_rc"] == 0 and r["video_rc"] == 0 and r["mame_rc"] == 0
        bad += 0 if ok else 1
        L.append("| %s | %d | %d | %s | %s | %s | %s | %.0f |" % (r["key"], r["frames"], n, runs or "—", verdict, cmds, video,
                                                                  r["t_mame"]))
    L.append("")
    with open(out_md, "w") as f:
        f.write("\n".join(L) + "\n")
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--blind", default=os.path.join(ROOT, "build", "sf2_blind"))
    ap.add_argument("--rom", default=os.path.join(ROOT, "build", "rom.bin"))
    ap.add_argument("--hints", default=os.path.join(ROOT, "tools", "hints.txt"))
    ap.add_argument("--sequences", default=os.path.join(ROOT, "tools", "sequences"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "coverage"))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--report-only", action="store_true")
    ap.add_argument("--step", action="store_true", help="correr con machine_step (cediendo en cada VBlank)")
    ap.add_argument("--mame", action="store_true", help="validar cada secuencia contra MAME (tras correrlas a ciegas)")
    ap.add_argument("--roms", default=os.path.join(ROOT, "sf2"), help="ROMs para las capturas del runtime")
    ap.add_argument("--snap-every", type=int, default=2000)
    ap.add_argument("--mame-report", action="store_true",
                    help="solo rehacer informe_mame.md con las comparaciones ya hechas de todas las secuencias")
    ap.add_argument("--roundtrip", action="store_true",
                    help="comprobar que guardar, reiniciar y cargar el estado a mitad de cada secuencia no cambia nada")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    seqs = sequences(args.sequences, args.only)
    if not seqs:
        raise SystemExit("no hay secuencias en %s" % args.sequences)
    logs = {}
    failed = []
    if not args.report_only:
        extra = ["--step"] if args.step else []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = [ex.submit(run_one, args.blind, args.rom, k, p, args.out, extra) for k, p in seqs]
            for fu in concurrent.futures.as_completed(futs):
                key, rc, dt, out = fu.result()
                lg = parse_log(out)
                logs[key] = lg
                state = "OK" if rc == 0 else "ERROR (%d)" % rc
                print("%-24s %s: %s cuadros, %s instrucciones, %s distintas, %.0f s%s" % (
                    key, state, lg.get("frames", "?"), format(lg.get("insns", 0), ","), lg.get("distinct", "?"), dt,
                    ("\n    " + lg["error"]) if "error" in lg else ""), flush=True)
                if rc != 0:
                    failed.append(key)
    from analyze import analyze
    an = analyze(args.rom, args.hints)
    keys = [k for k, _ in seqs]
    md = os.path.join(args.out, "informe_cobertura.md")
    problems, ex_i, st_i, ex_f, st_f = report(args.out, keys, logs, an, md)
    print("cobertura total: %d de %d instrucciones (%.1f %%), %d de %d funciones (%.1f %%); informe en %s" % (
        ex_i, st_i, 100.0 * ex_i / st_i, ex_f, st_f, 100.0 * ex_f / st_f, md))
    if args.mame_report:
        results = mame_results_from_disk(args.out, seqs, args.snap_every)
        md2 = os.path.join(args.out, "informe_mame.md")
        bad = mame_report(results, md2)
        print("validación contra MAME: %d de %d secuencias aceptadas; informe en %s" % (len(results) - bad, len(results), md2))
        return 1 if bad else 0
    if args.roundtrip:
        rts = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = [ex.submit(roundtrip_one, k, p, args.out, args.blind, args.rom, args.roms) for k, p in seqs]
            for fu in concurrent.futures.as_completed(futs):
                r = fu.result()
                rts.append(r)
                ok = r["summary_same"] and r["wav_same"] and r["rc"] == (0, 0)
                print("%-24s ida y vuelta de estado en %d puntos: resumen %s, WAV %s (%.0f MB), %.0f s%s" % (
                    r["key"], r["points"], "idéntico" if r["summary_same"] else "DISTINTO",
                    "idéntico" if r["wav_same"] else "DISTINTO", r["wav_bytes"] / 1e6, r["t"], "" if ok else "  <-- FALLA"),
                    flush=True)
                problems += 0 if ok else 1
        with open(os.path.join(args.out, "informe_estados.md"), "w") as f:
            f.write("# Guardar y cargar el estado a mitad de cada secuencia (hito 6)\n\n")
            f.write("`tools/coverage.py --roundtrip`: cada secuencia corre dos veces con sonido, seguida y con `sf2_blind "
                    "--roundtrip` (en cada punto guarda el estado completo, reinicia la máquina con la RAM llena de basura y "
                    "carga el estado). El resumen por cuadro y el WAV de 48 kHz tienen que salir idénticos.\n\n")
            f.write("| Secuencia | Puntos | Resumen | WAV | SHA-256 del resumen |\n|---|---:|---|---|---|\n")
            for r in sorted(rts, key=lambda r: r["key"]):
                f.write("| %s | %d | %s | %s | `%s…` |\n" % (r["key"], r["points"], "idéntico" if r["summary_same"] else "DISTINTO",
                                                          "idéntico" if r["wav_same"] else "DISTINTO", r["summary_sha"][:16]))
    if args.mame:
        results = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = [ex.submit(mame_one, k, p, args.out, args.blind, args.rom, args.roms, args.snap_every) for k, p in seqs]
            for fu in concurrent.futures.as_completed(futs):
                r = fu.result()
                results.append(r)
                n, runs, verdict = summary_digest(r["summary_out"])
                print("%-24s MAME: resumen %s (%d cuadros distintos%s), sonido %s, vídeo %s, %.0f s" % (
                    r["key"], "OK" if r["summary_rc"] == 0 else "FALLA", n, (": " + runs) if runs else "",
                    "OK" if r["cmds_rc"] == 0 else "FALLA", "OK" if r["video_rc"] == 0 else "FALLA", r["t_mame"]), flush=True)
        md2 = os.path.join(args.out, "informe_mame.md")
        bad = mame_report(results, md2)
        print("validación contra MAME: %d de %d secuencias aceptadas; informe en %s" % (len(results) - bad, len(results), md2))
        problems += bad
    if failed or problems:
        print("PROBLEMAS: %s" % (", ".join(failed) if failed else "%d (ver los informes)" % problems))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
