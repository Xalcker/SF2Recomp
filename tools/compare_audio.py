#!/usr/bin/env python3
"""compare_audio.py - compara el sonido del runtime con el de MAME (hito 4).

Lee dos directorios con el formato de tools/mame_audio.lua / build/sf2_blind --audio-dir:

  events.txt   F n t68 | L k v t68 tz | W a v tz | R a v tz
  ym.f32       salida del YM2151, float32 L R intercalados, 55.930 Hz
  oki.f32      salida del OKI, float32, 7.575 Hz
  mono.f32     mezcla del altavoz, float32, 48.000 Hz
  chunks.txt   muestras por actualización periódica
  sf2.wav / sf2_mame.wav   la mezcla como WAV de 16 bits (runtime: --wav; MAME: -wavwrite)

y compara, en este orden (cada nivel explica el siguiente):

  1. comandos del 68000: secuencia de escrituras en los latches (valor y cuadro en que se escriben) y la
     diferencia de instante entre los dos lados;
  2. accesos del Z80 a 0xF000-0xF00F: escrituras (dirección, valor, ciclo) y lecturas (dirección, valor
     leído, ciclo).  Iguales en valor y ciclo = el Z80 ejecuta lo mismo al mismo ritmo y ve lo mismo;
  3. muestras de los chips y de la mezcla: muestras idénticas bit a bit, primera diferente, diferencia
     máxima y relación señal/ruido, también por tramos (arranque, atract, combate...) con --sections;
  4. el WAV de la mezcla contra el WAV de MAME, por tramos de la partida, con criterios de aceptación
     que no exigen igualdad muestra a muestra (el remuestreo o el redondeo de otra plataforma la rompen):

       energía   RMS del tramo en dBFS: la diferencia entre los dos lados no pasa de --tol-energy (0,5 dB).
                 Un error de ganancia de una ruta (YM 0,35 / OKI 0,30) o un canal que falta da más de 1 dB;
                 el redondeo a 16 bits, menos de 0,001 dB en un tramo con señal.  Un tramo con RMS por
                 debajo de -80 dBFS (1 LSB de 16 bits son -90 dBFS) cuenta como silencio: dos tramos en
                 silencio son iguales aunque difieran en un LSB; uno en silencio y otro no, no.
       espectro  hasta --windows ventanas de 2048 muestras (Hann, 23,4 Hz por bin) repartidas por las partes
                 con señal del tramo, con el espectro de potencia promediado: (a) la energía de cada una de
                 las 9 bandas de octava (0-100 Hz, 100-200 ... 12,8-24 kHz) no se separa más de --tol-band
                 (1,0 dB), solo en las bandas a menos de 60 dB de la más fuerte del tramo (por debajo de eso
                 es ruido de cuantización); (b) la distancia espectral logarítmica (raíz de la media del
                 cuadrado de la diferencia en dB por bin, en los bins a menos de 60 dB del máximo) no pasa
                 de --tol-lsd (1,0 dB).
       exacto    (informativo) muestras distintas, diferencia máxima en LSB y señal/ruido del tramo.

  python3 tools/compare_audio.py build/audio/recomp build/audio/mame [--sections] [--report informe.md]
  python3 tools/compare_audio.py runtime.wav mame.wav            # solo el nivel 4, con los dos WAV

Código de salida: 0 si todo coincide (y los tramos del WAV pasan los criterios), 1 si hay diferencias.
"""
import argparse
import cmath
import math
import os
import struct
import sys
from array import array

RATES = {"ym": 55930, "oki": 7575, "mono": 48000}
Z80_HZ = 3579545
M68K_HZ = 10000000
FRAME_CYCLES = 167680            # ciclos del 68000 por cuadro (runtime/cps1.h)
FFT_N = 2048
BANDS_HZ = [0, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 24000]
FLOOR_DB = 60.0                  # por debajo de esto respecto al máximo del tramo no se exige coincidencia
SILENCE_DBFS = -80.0             # un tramo con RMS por debajo de esto cuenta como silencio (1 LSB de 16 bits = -90 dBFS)

# tramos de la partida de tools/inputs_sf2.txt (cuadro de inicio, nombre)
MARKS = [(0, "arranque y test de RAM"), (1, "aviso y atract"), (600, "moneda"), (1000, "start y selección"),
         (1500, "combate"), (3702, "KO, continuar, fin de partida"), (5000, "atract y demo")]

LOG = []                         # todo lo que se imprime, para --report


def say(*a):
    text = " ".join(str(x) for x in a)
    print(text)
    LOG.append(text)


def load_events(path):
    ev = {"F": [], "L": [], "W": [], "R": []}
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            k = p[0]
            if k == "F":
                ev["F"].append((int(p[1]), int(p[2])))
            elif k == "L":
                ev["L"].append((int(p[1]), int(p[2], 16), int(p[3]), int(p[4])))
            elif k in ("W", "R"):
                ev[k].append((int(p[1], 16), int(p[2], 16), int(p[3])))
    for k in ev:
        if k in ("W", "R"):
            ev[k].sort(key=lambda e: e[2])      # MAME escribe en orden de planificador, no de tiempo
    ev["F"].sort()
    return ev


def frame_of(t68, frames):
    """cuadro en curso en el ciclo t68 del 68000 (último punto de muestra anterior), 0 antes del primero"""
    lo, hi = 0, len(frames)
    while lo < hi:
        mid = (lo + hi) // 2
        if frames[mid][1] <= t68:
            lo = mid + 1
        else:
            hi = mid
    return frames[lo - 1][0] if lo else 0


def z80_frame(tz, frames):
    return frame_of(tz * M68K_HZ // Z80_HZ, frames)


def compare_latches(a, b, na, nb):
    la, lb = a["L"], b["L"]
    say(f"comandos del 68000 (latches): {na} {len(la)}, {nb} {len(lb)}")
    n = min(len(la), len(lb))
    bad = 0
    dts = []
    for i in range(n):
        (ka, va, ta, _), (kb, vb, tb, _) = la[i], lb[i]
        fa, fb = frame_of(ta, a["F"]), frame_of(tb, b["F"])
        if ka != kb or va != vb or fa != fb:
            if bad < 10:
                say(f"  DIFERENCIA en el comando {i}: {na} latch{ka}={va:02x} cuadro {fa} (t68 {ta}), "
                      f"{nb} latch{kb}={vb:02x} cuadro {fb} (t68 {tb})")
            bad += 1
        dts.append(ta - tb)
    if len(la) != len(lb):
        say(f"  número distinto de comandos ({len(la)} / {len(lb)})")
        bad += 1
    if dts:
        dts_sorted = sorted(dts)
        say(f"  {n - (bad if bad <= n else n)} de {n} iguales en valor y cuadro; diferencia de instante "
              f"({na} - {nb}) en ciclos del 68000: mínima {dts_sorted[0]}, mediana {dts_sorted[len(dts)//2]}, "
              f"máxima {dts_sorted[-1]}")
    return bad == 0


def compare_frames(a, b, na, nb):
    fa, fb = dict(a["F"]), dict(b["F"])
    common = sorted(set(fa) & set(fb))
    if not common:
        say("cuadros: ninguno en común")
        return
    d = [fa[f] - fb[f] for f in common]
    ds = sorted(d)
    say(f"cuadros: {len(fa)} / {len(fb)}; punto de muestra ({na} - {nb}) en ciclos del 68000: mínimo {ds[0]}, "
          f"mediana {ds[len(ds)//2]}, máximo {ds[-1]}")


def describe_access(e, frames):
    a, v, t = e
    return f"{a:04x}={v:02x} @{t} (cuadro {z80_frame(t, frames)})"


def compare_accesses(kind, a, b, na, nb, context):
    ea, eb = a[kind], b[kind]
    name = "escrituras" if kind == "W" else "lecturas"
    n = min(len(ea), len(eb))
    first = None
    same_val = 0
    same_all = 0
    for i in range(n):
        if ea[i][0] == eb[i][0] and ea[i][1] == eb[i][1]:
            same_val += 1
            if ea[i][2] == eb[i][2]:
                same_all += 1
            elif first is None:
                first = i
        elif first is None:
            first = i
    say(f"Z80 {name} en 0xF000-0xF00F: {na} {len(ea)}, {nb} {len(eb)}; {same_all} de {n} iguales en dirección, "
          f"valor y ciclo; {same_val} iguales en dirección y valor")
    ok = first is None and len(ea) == len(eb)
    if first is not None:
        say(f"  primera diferencia en el acceso {first}:")
        for i in range(max(0, first - context), min(n, first + context + 1)):
            mark = ">>" if i == first else "  "
            say(f"  {mark} {na}: {describe_access(ea[i], a['F']):40s} {nb}: {describe_access(eb[i], b['F'])}")
    elif len(ea) != len(eb):
        say(f"  iguales en la parte común; uno de los lados tiene {abs(len(ea) - len(eb))} accesos más")
    return ok


def load_f32(path):
    arr = array("f")
    with open(path, "rb") as f:
        arr.frombytes(f.read())
    if sys.byteorder != "little":
        arr.byteswap()
    return arr


def compare_stream(name, pa, pb, na, nb, channels, sections):
    a, b = load_f32(pa), load_f32(pb)
    rate = RATES[name]
    n = min(len(a), len(b)) // channels * channels
    ba = a[:n].tobytes()
    bb = b[:n].tobytes()
    block = 4096 * channels
    first = None
    diff_count = 0
    maxd = 0.0
    sig = noise = 0.0
    for off in range(0, n, block):
        end = min(off + block, n)
        if ba[off * 4:end * 4] == bb[off * 4:end * 4]:
            continue
        for i in range(off, end):
            if a[i] != b[i]:
                if first is None:
                    first = i
                diff_count += 1
                d = abs(a[i] - b[i])
                if d > maxd:
                    maxd = d
    if diff_count:
        for i in range(n):
            sig += b[i] * b[i]
            d = a[i] - b[i]
            noise += d * d
    frames = n // channels
    secs = frames / rate
    status = "IDÉNTICO" if diff_count == 0 else "DIFERENTE"
    if diff_count == 0 and len(a) != len(b):
        status += " (en la parte común; los lados paran en instantes distintos)"
    say(f"{name}.f32 ({rate} Hz, {channels} canal{'es' if channels > 1 else ''}): {na} {len(a)//channels} muestras, "
          f"{nb} {len(b)//channels}; comparadas {frames} ({secs:.2f} s): {status}")
    if diff_count:
        t = (first // channels) / rate
        snr = 10 * math.log10(sig / noise) if noise > 0 and sig > 0 else float("inf")
        say(f"  {diff_count} valores distintos de {n}; primero en la muestra {first // channels} ({t:.4f} s), "
              f"diferencia máxima {maxd:.6g}, señal/ruido {snr:.1f} dB")
    if sections and frames:
        for lo, hi, label in sections:
            i0, i1 = int(lo * rate) * channels, min(int(hi * rate), frames) * channels
            if i0 >= i1:
                continue
            eq = a[i0:i1].tobytes() == b[i0:i1].tobytes()
            energy = sum(x * x for x in b[i0:i1:max(1, (i1 - i0) // 20000 or 1)])
            say(f"    {label:28s} {lo:7.2f}-{min(hi, secs):7.2f} s: {'idéntico' if eq else 'distinto'}"
                  f"{'' if energy > 0 else ' (silencio)'}")
    return diff_count == 0


def build_sections(fr, last):
    """tramos (inicio s, fin s, nombre); `fr` = {cuadro: ciclo del 68000} de MAME, o None para usar el cuadro nominal"""
    def tsec(frame):
        if not fr:
            return frame * FRAME_CYCLES / M68K_HZ
        f = min(fr, key=lambda k: abs(k - frame)) if frame not in fr else frame
        return fr[f] / M68K_HZ
    out = []
    for i, (f0, label) in enumerate(MARKS):
        if f0 > last:
            break
        f1 = MARKS[i + 1][0] if i + 1 < len(MARKS) else last
        out.append((0.0 if f0 == 0 else tsec(f0), tsec(min(f1, last)) if f1 else tsec(1), label))
    return out


# ---- WAV: energía y espectro por tramos ---------------------------------------------------------------

def read_wav(path):
    """(frecuencia, canales, muestras int16 intercaladas) de un WAV PCM de 16 bits"""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        sys.exit(f"{path}: no es un WAV")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(d):
        tag, size = d[pos:pos + 4], struct.unpack_from("<I", d, pos + 4)[0]
        body = d[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body)
        elif tag == b"data":
            data = body
            break
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        sys.exit(f"{path}: faltan los bloques fmt o data")
    if fmt[0] != 1 or fmt[5] != 16:
        sys.exit(f"{path}: solo se admite PCM de 16 bits (formato {fmt[0]}, {fmt[5]} bits)")
    samples = array("h")
    samples.frombytes(data[:len(data) // 2 * 2])
    if sys.byteorder != "little":
        samples.byteswap()
    return fmt[2], fmt[1], samples


def make_fft(n):
    levels = n.bit_length() - 1
    rev = [int(format(i, "0%db" % levels)[::-1], 2) for i in range(n)]
    tw = [cmath.exp(-2j * math.pi * k / n) for k in range(n // 2)]

    def fft(x):
        a = [x[r] for r in rev]
        size = 2
        while size <= n:
            half, step = size // 2, n // size
            for start in range(0, n, size):
                k = 0
                for j in range(start, start + half):
                    t = a[j + half] * tw[k]
                    u = a[j]
                    a[j] = u + t
                    a[j + half] = u - t
                    k += step
            size *= 2
        return a
    return fft


def db(x):
    return 10 * math.log10(x) if x > 0 else float("-inf")


def analyze_section(a, b, i0, i1, rate, fft, hann, windows):
    """métricas de A frente a B (muestras int16) en [i0, i1)"""
    sa, sb = a[i0:i1], b[i0:i1]
    n = len(sa)
    r = {"n": n}
    r["exact"] = sa == sb
    ea = sum(x * x for x in sa)
    eb = sum(x * x for x in sb)
    err = sum((x - y) * (x - y) for x, y in zip(sa, sb)) if not r["exact"] else 0
    r["ndiff"] = sum(1 for x, y in zip(sa, sb) if x != y) if not r["exact"] else 0
    r["maxd"] = max((abs(x - y) for x, y in zip(sa, sb)), default=0) if not r["exact"] else 0
    r["rms_a"] = db(ea / n / 32768.0 ** 2) if n else float("-inf")   # dBFS: potencia media / FS^2
    r["rms_b"] = db(eb / n / 32768.0 ** 2) if n else float("-inf")
    r["silent_a"], r["silent_b"] = r["rms_a"] <= SILENCE_DBFS, r["rms_b"] <= SILENCE_DBFS
    r["snr"] = float("inf") if err == 0 else (db(eb / err) if eb > 0 else float("-inf"))
    # energía
    if r["silent_a"] and r["silent_b"]:
        r["d_energy"] = 0.0
    elif r["silent_a"] or r["silent_b"]:
        r["d_energy"] = float("inf")
    else:
        r["d_energy"] = r["rms_a"] - r["rms_b"]
    # espectro: ventanas repartidas por las partes con señal
    r["bands"] = None
    r["lsd"] = 0.0
    r["d_band"] = 0.0
    r["windows"] = 0
    if r["silent_a"] and r["silent_b"]:
        return r
    cands = [s for s in range(0, n - FFT_N + 1, FFT_N * 4) if any(sa[s:s + FFT_N]) or any(sb[s:s + FFT_N])]
    if not cands:
        return r
    if len(cands) > windows:
        cands = [cands[int(k * (len(cands) - 1) / max(1, windows - 1))] for k in range(windows)]
    half = FFT_N // 2 + 1
    pa, pb = [0.0] * half, [0.0] * half
    for s0 in cands:
        z = [complex(sa[s0 + k] * hann[k], sb[s0 + k] * hann[k]) for k in range(FFT_N)]
        zf = fft(z)
        for k in range(half):
            zk, zn = zf[k], zf[(-k) % FFT_N].conjugate()
            xa = (zk + zn) / 2
            xb = (zk - zn) / 2j
            pa[k] += xa.real * xa.real + xa.imag * xa.imag
            pb[k] += xb.real * xb.real + xb.imag * xb.imag
    r["windows"] = len(cands)
    hz = rate / FFT_N
    bands = []
    for lo, hi in zip(BANDS_HZ, BANDS_HZ[1:]):
        k0, k1 = int(math.ceil(lo / hz)), min(half, int(math.ceil(hi / hz)))
        bands.append((lo, hi, sum(pa[k0:k1]), sum(pb[k0:k1])))
    top = max(max(e[2], e[3]) for e in bands)
    r["bands"] = bands
    worst = 0.0
    for lo, hi, x, y in bands:
        if max(x, y) < top * 10 ** (-FLOOR_DB / 10):
            continue
        d = abs(db(x) - db(y)) if x > 0 and y > 0 else float("inf")
        worst = max(worst, d)
    r["d_band"] = worst
    peak = max(max(pa), max(pb))
    acc, cnt = 0.0, 0
    for x, y in zip(pa, pb):
        if max(x, y) < peak * 10 ** (-FLOOR_DB / 10):
            continue
        d = abs(db(x) - db(y)) if x > 0 and y > 0 else FLOOR_DB
        acc += d * d
        cnt += 1
    r["lsd"] = math.sqrt(acc / cnt) if cnt else 0.0
    return r


def compare_wav(path_a, path_b, na, nb, sections, tol, windows):
    """compara dos WAV por tramos; devuelve (ok, filas)"""
    ra, ca, a = read_wav(path_a)
    rb, cb, b = read_wav(path_b)
    say(f"WAV: {na} {os.path.basename(path_a)} {len(a) // ca} muestras, {ra} Hz, {ca} canal(es); "
        f"{nb} {os.path.basename(path_b)} {len(b) // cb} muestras, {rb} Hz, {cb} canal(es)")
    if (ra, ca) != (rb, cb):
        say("  DIFERENTE: distinto formato (frecuencia o canales)")
        return False, []
    if ca != 1:
        a = array("h", [sum(a[i:i + ca]) // ca for i in range(0, len(a) - ca + 1, ca)])
        b = array("h", [sum(b[i:i + ca]) // ca for i in range(0, len(b) - ca + 1, ca)])
    n = min(len(a), len(b))
    secs = n / ra
    if len(a) != len(b):
        say(f"  longitudes distintas ({len(a)} y {len(b)}): se compara la parte común, {n} muestras ({secs:.2f} s)")
    whole_exact = a[:n] == b[:n]
    say(f"  muestras de la parte común: {'idénticas' if whole_exact else 'hay diferencias'}")
    fft, hann = make_fft(FFT_N), [0.5 - 0.5 * math.cos(2 * math.pi * k / FFT_N) for k in range(FFT_N)]
    rows, ok = [], True
    for lo, hi, label in sections:
        i0, i1 = int(lo * ra), min(int(hi * ra), n)
        if i1 - i0 < FFT_N:
            continue
        r = analyze_section(a, b, i0, i1, ra, fft, hann, windows)
        r.update(label=label, lo=lo, hi=min(hi, secs))
        good_energy = abs(r["d_energy"]) <= tol["energy"]
        good_band = r["d_band"] <= tol["band"]
        good_lsd = r["lsd"] <= tol["lsd"]
        r["pass"] = good_energy and good_band and good_lsd
        ok &= r["pass"]
        rows.append(r)
        if r["silent_a"] and r["silent_b"]:
            kind = "silencio en los dos"
        else:
            kind = (f"RMS {r['rms_a']:.2f}/{r['rms_b']:.2f} dBFS (dif {r['d_energy']:+.3f} dB), bandas {r['d_band']:.3f} dB, "
                    f"LSD {r['lsd']:.3f} dB, {r['windows']} ventanas")
        if r["exact"]:
            exact = "exacto"
        else:
            snr = "" if r["silent_a"] and r["silent_b"] else f", SNR {r['snr']:.1f} dB"
            exact = f"{r['ndiff']} muestras distintas, máx {r['maxd']} LSB{snr}"
        say(f"    {label:30s} {r['lo']:7.2f}-{r['hi']:7.2f} s: {'PASA' if r['pass'] else 'FALLA'}  {kind}; {exact}")
    say(f"WAV: {'todos los tramos pasan los criterios' if ok else 'hay tramos que no pasan los criterios'}"
        f" (energía ±{tol['energy']} dB, bandas ±{tol['band']} dB, LSD ≤ {tol['lsd']} dB)")
    return ok, rows


def check_reference_hook(dir_b, wav_path, nb):
    """el WAV de -wavwrite de MAME debe ser la cuantización del flujo mono.f32 del gancho de Lua"""
    mono = os.path.join(dir_b, "mono.f32")
    if not os.path.exists(mono) or not os.path.exists(wav_path):
        return True
    _, _, w = read_wav(wav_path)
    f = load_f32(mono)
    n = min(len(f), len(w))
    q = array("h", [max(-32768, min(32767, int(x * 32768))) for x in f[:n]])
    same = q == w[:n]
    say(f"referencia: el WAV de {nb} (-wavwrite) {'es' if same else 'NO es'} la cuantización de mono.f32 del gancho de Lua "
        f"({n} muestras; {len(f)} en mono.f32, {len(w)} en el WAV)")
    return same


def write_report(path, args, ok, rows, tol, hook_ok):
    lines = ["# Informe de comparación de audio (hito 4)", "",
             f"Comando: `python3 tools/compare_audio.py {' '.join(sys.argv[1:])}`", "",
             f"Resultado: **{'todo coincide' if ok else 'hay diferencias'}**", ""]
    if rows:
        lines += ["## WAV por tramos: energía y espectro", "",
                  f"Criterios (por tramo): energía RMS ±{tol['energy']} dB; energía por banda de octava ±{tol['band']} dB "
                  f"(bandas a menos de {FLOOR_DB:.0f} dB de la más fuerte); distancia espectral logarítmica ≤ {tol['lsd']} dB "
                  f"(ventanas de {FFT_N} muestras, Hann).  La igualdad muestra a muestra se informa pero no se exige.", "",
                  "| Tramo | Desde (s) | Hasta (s) | RMS recomp (dBFS) | RMS mame (dBFS) | Dif. energía (dB) | "
                  "Dif. banda máx. (dB) | LSD (dB) | Muestras distintas | Máx. dif. (LSB) | Resultado |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|"]
        for r in rows:
            sil = r["silent_a"] and r["silent_b"]
            lines.append(f"| {r['label']} | {r['lo']:.2f} | {r['hi']:.2f} | "
                         f"{'silencio' if sil else format(r['rms_a'], '.2f')} | {'silencio' if sil else format(r['rms_b'], '.2f')} | "
                         f"{r['d_energy']:+.3f} | {r['d_band']:.3f} | {r['lsd']:.3f} | {r['ndiff']} | {r['maxd']} | "
                         f"{'pasa' if r['pass'] else 'falla'} |")
        lines.append("")
        for r in rows:
            if r["bands"]:
                lines.append(f"Bandas de octava en «{r['label']}» (potencia en dB re. banda más fuerte de los dos lados):")
                top = max(max(e[2], e[3]) for e in r["bands"])
                cells = [f"{lo}-{hi} Hz: {db(x / top) + 0.0:.1f} / {db(y / top) + 0.0:.1f}" for lo, hi, x, y in r["bands"]]
                lines.append("")
                lines.append("`" + "; ".join(cells) + "` (recomp / mame)")
                lines.append("")
        if not hook_ok:
            lines += ["**Aviso:** el WAV de MAME no es la cuantización de `mono.f32` del gancho de Lua.", ""]
    lines += ["## Salida completa", "", "```text"] + LOG + ["```", ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dir_a", help="directorio del runtime (build/sf2_blind --audio-dir) o su WAV")
    ap.add_argument("dir_b", help="directorio de MAME (tools/mame_audio.lua) o su WAV")
    ap.add_argument("--name-a", default="recomp")
    ap.add_argument("--name-b", default="mame")
    ap.add_argument("--context", type=int, default=4)
    ap.add_argument("--sections", action="store_true", help="resultado por tramos de la partida de inputs_sf2.txt")
    ap.add_argument("--report", metavar="ARCHIVO", help="escribe el informe en Markdown")
    ap.add_argument("--no-wav", action="store_true", help="no comparar los WAV de los directorios")
    ap.add_argument("--wav-a", default="sf2.wav", help="WAV dentro de dir_a (por defecto sf2.wav)")
    ap.add_argument("--wav-b", default="sf2_mame.wav", help="WAV dentro de dir_b (por defecto sf2_mame.wav)")
    ap.add_argument("--windows", type=int, default=32, help="ventanas espectrales por tramo")
    ap.add_argument("--tol-energy", type=float, default=0.5, help="dB de diferencia de energía RMS por tramo")
    ap.add_argument("--tol-band", type=float, default=1.0, help="dB de diferencia por banda de octava")
    ap.add_argument("--tol-lsd", type=float, default=1.0, help="dB de distancia espectral logarítmica")
    args = ap.parse_args()
    na, nb = args.name_a, args.name_b
    tol = {"energy": args.tol_energy, "band": args.tol_band, "lsd": args.tol_lsd}

    wav_only = os.path.isfile(args.dir_a) and os.path.isfile(args.dir_b)
    ok = True
    rows = []
    hook_ok = True
    fr = None
    last = 0
    if not wav_only:
        a = load_events(os.path.join(args.dir_a, "events.txt"))
        b = load_events(os.path.join(args.dir_b, "events.txt"))
        # los dos lados no paran en el mismo instante (MAME sale en el punto de muestra del último cuadro, el
        # runtime al entregar la IRQ siguiente): se compara hasta el último punto de muestra común
        if a["F"] and b["F"]:
            last = min(a["F"][-1][0], b["F"][-1][0])
            t68_end = min(dict(a["F"])[last], dict(b["F"])[last])
            tz_end = t68_end * Z80_HZ // M68K_HZ
            for ev in (a, b):
                ev["F"] = [e for e in ev["F"] if e[0] <= last]
                ev["L"] = [e for e in ev["L"] if e[2] <= t68_end]
                ev["W"] = [e for e in ev["W"] if e[2] <= tz_end]
                ev["R"] = [e for e in ev["R"] if e[2] <= tz_end]
            say(f"se compara hasta el cuadro {last} (ciclo {t68_end} del 68000, {t68_end / M68K_HZ:.2f} s)")
        compare_frames(a, b, na, nb)
        ok &= compare_latches(a, b, na, nb)
        ok &= compare_accesses("W", a, b, na, nb, args.context)
        ok &= compare_accesses("R", a, b, na, nb, args.context)
        fr = dict(b["F"])

    sections = build_sections(fr, last) if not wav_only else None
    if not wav_only:
        for name, ch in (("ym", 2), ("oki", 1), ("mono", 1)):
            pa, pb = os.path.join(args.dir_a, name + ".f32"), os.path.join(args.dir_b, name + ".f32")
            if os.path.exists(pa) and os.path.exists(pb):
                ok &= compare_stream(name, pa, pb, na, nb, ch, sections if args.sections else None)

    if wav_only:
        wa, wb = args.dir_a, args.dir_b
        # tramos por el cuadro nominal; el último es el que cabe en la duración común
        rate, ch, w = read_wav(wb)
        ev_b = os.path.join(os.path.dirname(os.path.abspath(wb)), "events.txt")
        if os.path.exists(ev_b):                    # el WAV de MAME con sus registros al lado: instantes exactos
            fr = dict(load_events(ev_b)["F"])
            last = max(fr)
        else:
            last = int(len(w) / ch / rate * M68K_HZ / FRAME_CYCLES)
            say("tramos por el cuadro nominal (167.680 ciclos); el primer cuadro real llega ~1,8 s más tarde")
        sections = build_sections(fr, last)
    else:
        wa, wb = os.path.join(args.dir_a, args.wav_a), os.path.join(args.dir_b, args.wav_b)
    if not args.no_wav and os.path.exists(wa) and os.path.exists(wb):
        wav_ok, rows = compare_wav(wa, wb, na, nb, sections, tol, args.windows)
        ok &= wav_ok
        if not wav_only:
            hook_ok = check_reference_hook(args.dir_b, wb, nb)
            ok &= hook_ok
    elif wav_only:
        sys.exit("faltan los WAV")
    say("RESULTADO: " + ("todo igual" if ok else "hay diferencias"))
    if args.report:
        write_report(args.report, args, ok, rows, tol, hook_ok)
        print(f"informe escrito en {args.report}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
