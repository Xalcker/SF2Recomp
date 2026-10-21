#!/usr/bin/env python3
"""playgen.py - genera secuencias de entradas que recorren el juego (hito 6).

Juega a SF2 sobre el núcleo recompilado (build/libsf2py.dylib, ctypes) y graba lo que pulsa, cuadro a cuadro, en el
formato de las secuencias de tools/ ("desde hasta tecla", ver tools/sequences/README.md).  No inventa nada que la
secuencia no reproduzca: las entradas se fijan antes de cada cuadro y se aplican en la entrega de su IRQ2, igual que al
leer la secuencia desde sf2_blind o desde tools/mame_blind.lua, así que la partida grabada se reproduce idéntica.

Cómo juega: en los combates hace una búsqueda codiciosa con estados guardados (prueba cada maniobra de un repertorio
del personaje, la deja correr con una continuación fija y se queda con la que más daño hace y menos recibe; si pierde
un asalto, vuelve al principio del asalto y lo repite con otra semilla).  Fuera de los combates sigue las pantallas
por las variables del juego (RAM de trabajo):

  0xFF8001  modo principal (atracción 0x04-0x10, título con créditos 0x04, partida 0x02)
  0xFF8005  fase de la partida (0 selección, 4 combate, 8 fin de combate / continuar, 0x0A, 0x0C...)
  0xFF8009  sub-fase del combate (0x08 presentación del asalto, 0x0A en juego, 0x0C KO...)
  0xFF83C0 / 0xFF86C0  estructuras de los jugadores 1 y 2: +0x0C x, +0x10 y, +0x30 vida (0x90 llena, -1 KO),
                       +0x289 personaje, +0x297 cursor de la pantalla de selección, +0x2A0 puntuación (BCD)
  0xFF82DD  créditos

Uso (ver --help):
  python3 tools/playgen.py --char ryu --out tools/sequences/ryu.txt
"""
import argparse
import ctypes
import os
import random
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CHARS = ["ryu", "ehonda", "blanka", "guile", "ken", "chunli", "zangief", "dhalsim"]
CHAR_NAMES = {"ryu": "Ryu", "ehonda": "E. Honda", "blanka": "Blanka", "guile": "Guile", "ken": "Ken",
              "chunli": "Chun-Li", "zangief": "Zangief", "dhalsim": "Dhalsim"}

# bits de entrada (activos a 1 aquí; el núcleo los invierte)
IN0 = {"coin1": 0x01, "coin2": 0x02, "service": 0x04, "start1": 0x10, "start2": 0x20, "test": 0x40}
R, L, D, U = 1, 2, 4, 8
LP, MP, HP = 0x10, 0x20, 0x40
LK, MK, HK = 1, 2, 4                     # en in2 (jugador 1); el jugador 2 los lleva << 4 en in2 y << 8 en in1
KEYNAMES = [  # (puerto, máscara, nombre) en el orden en que se escriben
    (0, 0x01, "coin1"), (0, 0x02, "coin2"), (0, 0x04, "service"), (0, 0x10, "start1"), (0, 0x20, "start2"),
    (0, 0x40, "test"),
    (1, 0x0001, "p1_right"), (1, 0x0002, "p1_left"), (1, 0x0004, "p1_down"), (1, 0x0008, "p1_up"),
    (1, 0x0010, "p1_lp"), (1, 0x0020, "p1_mp"), (1, 0x0040, "p1_hp"),
    (1, 0x0100, "p2_right"), (1, 0x0200, "p2_left"), (1, 0x0400, "p2_down"), (1, 0x0800, "p2_up"),
    (1, 0x1000, "p2_lp"), (1, 0x2000, "p2_mp"), (1, 0x4000, "p2_hp"),
    (2, 0x01, "p1_lk"), (2, 0x02, "p1_mk"), (2, 0x04, "p1_hk"),
    (2, 0x10, "p2_lk"), (2, 0x20, "p2_mk"), (2, 0x40, "p2_hk"),
]
KEYS = {n: (p, m) for p, m, n in KEYNAMES}

P1, P2 = 0xFF83C0, 0xFF86C0
OFF_X, OFF_Y, OFF_HP, OFF_CHAR, OFF_CURSOR, OFF_SCORE = 0x0C, 0x10, 0x30, 0x289, 0x297, 0x2A0
CREDITS = 0xFF82DD
BONUS_SCORE = 0xFF8518      # puntos de la fase de bonus (BCD)
PROGRESS = 0xFF82F7         # combates ganados en la partida


# ---- la máquina ---------------------------------------------------------------------------------------------
class Machine:
    def __init__(self, lib_path, roms, video=False):
        lib = ctypes.CDLL(lib_path)
        vp = ctypes.c_void_p
        lib.sf2py_create.restype = vp
        lib.sf2py_create.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        lib.sf2py_destroy.argtypes = [vp]
        lib.sf2py_set_input.argtypes = [vp, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.sf2py_set_dips.argtypes = [vp, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.sf2py_frame.restype = ctypes.c_ulonglong
        lib.sf2py_frame.argtypes = [vp]
        lib.sf2py_wram.restype = ctypes.c_void_p
        lib.sf2py_wram.argtypes = [vp]
        lib.sf2py_run.argtypes = [vp, ctypes.c_int]
        lib.sf2py_state_size.argtypes = [vp]
        lib.sf2py_state_save.argtypes = [vp, ctypes.c_void_p, ctypes.c_int]
        lib.sf2py_state_load.argtypes = [vp, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        lib.sf2py_state_crc.restype = ctypes.c_uint
        lib.sf2py_state_crc.argtypes = [vp]
        lib.sf2py_png.argtypes = [vp, ctypes.c_char_p]
        lib.sf2py_error.restype = ctypes.c_char_p
        lib.sf2py_error.argtypes = [vp]
        self.lib = lib
        err = ctypes.create_string_buffer(4096)
        self.s = lib.sf2py_create(roms.encode(), 1 if video else 0, 0, err, len(err))
        if not self.s:
            raise SystemExit("playgen: " + err.value.decode("utf-8", "replace"))
        self.ram = (ctypes.c_uint8 * 0x10000).from_address(lib.sf2py_wram(self.s))
        self.err = ctypes.create_string_buffer(512)

    def set_dips(self, a, b, c):
        self.lib.sf2py_set_dips(self.s, a, b, c)

    def frame(self):
        return self.lib.sf2py_frame(self.s)

    def step(self, bits):
        """corre hasta que el juego cuente un cuadro más con las entradas `bits` = (in0, in1, in2); devuelve los
        cuadros entregados (normalmente 1, a veces 2 si la IRQ estaba enmascarada en el VBlank anterior)"""
        f0 = self.frame()
        self.lib.sf2py_set_input(self.s, bits[0], bits[1], bits[2])
        if self.lib.sf2py_run(self.s, 1) < 0:
            raise RuntimeError(self.lib.sf2py_error(self.s).decode("utf-8", "replace"))
        return range(f0 + 1, self.frame() + 1)

    def save(self):
        n = self.lib.sf2py_state_size(self.s)
        buf = ctypes.create_string_buffer(n)
        if self.lib.sf2py_state_save(self.s, buf, n) != n:
            raise RuntimeError("no se pudo guardar el estado")
        return buf

    def load(self, buf):
        if self.lib.sf2py_state_load(self.s, buf, len(buf), self.err, len(self.err)) < 0:
            raise RuntimeError(self.err.value.decode())

    def state_crc(self):
        return self.lib.sf2py_state_crc(self.s)

    def png(self, path):
        self.lib.sf2py_png(self.s, path.encode())

    # RAM de trabajo (0xFF0000-0xFFFFFF)
    def b(self, a):
        return self.ram[a - 0xFF0000]

    def w(self, a):
        o = a - 0xFF0000
        return (self.ram[o] << 8) | self.ram[o + 1]

    def sw(self, a):
        v = self.w(a)
        return v - 0x10000 if v & 0x8000 else v

    def bcd(self, a, n=4):
        v = 0
        for i in range(n):
            x = self.b(a + i)
            v = v * 100 + (x >> 4) * 10 + (x & 15)
        return v


# ---- la grabación ---------------------------------------------------------------------------------------------
class Recorder:
    """entradas por cuadro del juego (numeración de las secuencias: cuadro N = N-ésima IRQ2)"""

    def __init__(self, m):
        self.m = m
        self.rec = {}

    def step(self, bits):
        for f in self.m.step(bits):
            if bits != (0, 0, 0):
                self.rec[f] = bits
            else:
                self.rec.pop(f, None)

    def hold(self, bits, n):
        for _ in range(n):
            self.step(bits)

    def truncate(self, frame):
        for f in [f for f in self.rec if f > frame]:
            del self.rec[f]

    def lines(self):
        """intervalos por tecla, ordenados por comienzo"""
        out = []
        for port, mask, name in KEYNAMES:
            frames = sorted(f for f, b in self.rec.items() if b[port] & mask)
            start = prev = None
            for f in frames:
                if start is None:
                    start = prev = f
                elif f == prev + 1:
                    prev = f
                else:
                    out.append((start, prev, name))
                    start = prev = f
            if start is not None:
                out.append((start, prev, name))
        out.sort(key=lambda t: (t[0], t[1], t[2]))
        return out


def p1(in1=0, in2=0):
    return (0, in1, in2)


# ---- maniobras de combate --------------------------------------------------------------------------------------
# Cada maniobra es una lista de (dirección relativa, botones in1, botones in2) por cuadro; F = hacia el rival.
def rel_bits(d, facing_right):
    f, b = (R, L) if facing_right else (L, R)
    return {"N": 0, "F": f, "B": b, "U": U, "D": D, "UF": U | f, "UB": U | b, "DF": D | f, "DB": D | b}[d]


def seq(*parts):
    out = []
    for d, n, p, k in parts:
        out += [(d, p, k)] * n
    return out


def common_moves():
    mv = {
        "espera": seq(("N", 6, 0, 0)),
        "avanza": seq(("F", 10, 0, 0)),
        "bloquea": seq(("B", 10, 0, 0)),
        "bloquea_agachado": seq(("DB", 10, 0, 0)),
        "salto_adelante": seq(("UF", 3, 0, 0), ("N", 30, 0, 0)),
        "salto_atras": seq(("UB", 3, 0, 0), ("N", 30, 0, 0)),
        "salto_patada": seq(("UF", 3, 0, 0), ("N", 14, 0, 0), ("N", 3, 0, HK), ("N", 16, 0, 0)),
        "salto_punetazo": seq(("UF", 3, 0, 0), ("N", 12, 0, 0), ("N", 3, HP, 0), ("N", 18, 0, 0)),
        "salto_vertical_patada": seq(("U", 3, 0, 0), ("N", 12, 0, 0), ("N", 3, 0, MK), ("N", 18, 0, 0)),
        "agarre": seq(("F", 2, 0, 0), ("F", 3, HP, 0)),
        "agarre_patada": seq(("F", 2, 0, 0), ("F", 3, 0, HK)),
    }
    for name, p, k in (("lp", LP, 0), ("mp", MP, 0), ("hp", HP, 0), ("lk", 0, LK), ("mk", 0, MK), ("hk", 0, HK)):
        mv[name] = seq(("N", 3, p, k), ("N", 4, 0, 0))
        mv["agachado_" + name] = seq(("D", 3, p, k), ("D", 4, 0, 0))
        mv["adelante_" + name] = seq(("F", 3, p, k), ("N", 4, 0, 0))
    return mv


def qcf(btn_p, btn_k=0):
    return seq(("D", 2, 0, 0), ("DF", 2, 0, 0), ("F", 2, btn_p, btn_k), ("N", 4, 0, 0))


def qcb(btn_p, btn_k=0):
    return seq(("D", 2, 0, 0), ("DB", 2, 0, 0), ("B", 2, btn_p, btn_k), ("N", 4, 0, 0))


def dp(btn_p):
    return seq(("F", 2, 0, 0), ("D", 2, 0, 0), ("DF", 2, btn_p, 0), ("N", 4, 0, 0))


def charge(first, then, btn_p, btn_k=0, n=56):
    return seq((first, n, 0, 0), (then, 3, btn_p, btn_k), ("N", 4, 0, 0))


def mash(btn_p, btn_k=0, n=5):
    out = []
    for _ in range(n):
        out += [("N", btn_p, btn_k), ("N", 0, 0)]
    return out


def char_moves(ch):
    mv = common_moves()
    if ch in ("ryu", "ken"):
        mv.update({"hadoken_f": qcf(HP), "hadoken_d": qcf(LP), "shoryuken_f": dp(HP), "shoryuken_d": dp(LP),
                   "tatsumaki": qcb(0, MK)})
    elif ch == "guile":
        mv.update({"sonic_boom": charge("B", "F", HP), "flash_kick": charge("DB", "U", 0, HK)})
    elif ch == "blanka":
        mv.update({"electricidad": mash(HP, 0, 6), "rodar": charge("B", "F", HP)})
    elif ch == "ehonda":
        mv.update({"cien_manos": mash(HP, 0, 6), "torpedo": charge("B", "F", HP)})
    elif ch == "chunli":
        mv.update({"patadas_rayo": mash(0, HK, 6), "pajaro": charge("D", "U", 0, HK)})
    elif ch == "zangief":
        mv.update({"lariat": seq(("N", 3, LP | MP | HP, 0), ("N", 4, 0, 0)),
                   "pildriver": seq(("F", 2, 0, 0), ("D", 2, 0, 0), ("B", 2, 0, 0), ("U", 2, HP, 0), ("N", 4, 0, 0))})
    elif ch == "dhalsim":
        mv.update({"yoga_fire": qcf(HP), "yoga_flame": seq(("F", 2, 0, 0), ("DF", 2, 0, 0), ("D", 2, 0, 0),
                                                              ("DB", 2, 0, 0), ("B", 2, HP, 0), ("N", 4, 0, 0))})
    return mv


# ---- el jugador ------------------------------------------------------------------------------------------------
class Player:
    def __init__(self, m, rec, args, log):
        self.m, self.rec, self.args, self.log = m, rec, args, log
        self.rng = random.Random(args.seed)
        self.moves = char_moves(args.char)
        self.rollout = seq(("B", args.horizon, 0, 0))
        self.events = []          # (cuadro, texto) para el informe
        self.round_saves = 0
        self.decisions = 0
        self.p2 = None

    # -- lectura del estado
    def hp(self, base):
        return self.m.sw(base + OFF_HP)

    def fighting(self):
        m = self.m
        return m.b(0xFF8001) == 2 and m.b(0xFF8005) == 4 and m.b(0xFF8009) == 0x0A

    def bonus(self):
        """fase de bonus: en juego (0xFF8009 = 0x0A) sin jugador 2 (su estructura, +6, queda a 0)"""
        return self.m.b(P2 + 6) == 0

    def facing_right(self):
        return self.m.w(P1 + OFF_X) < self.m.w(P2 + OFF_X)

    def score(self):
        return self.m.bcd(P1 + OFF_SCORE)

    def value(self):
        if self.bonus():
            return self.m.bcd(BONUS_SCORE) / 10.0
        h1, h2 = max(self.hp(P1), 0), max(self.hp(P2), 0)
        v = (0x90 - h2) - 1.15 * (0x90 - h1) + self.score() / 100.0
        if self.hp(P2) < 0:
            v += 300
        if self.hp(P1) < 0:
            v -= 300
        return v

    # -- ejecución de una maniobra
    def play(self, moves, facing, commit=True):
        for d, p, k in moves:
            bits = p1(rel_bits(d, facing) | p, k)
            if self.p2 is not None:
                b2 = self.p2.bits(self.m.frame() + 1)
                bits = (bits[0] | b2[0], bits[1] | b2[1], bits[2] | b2[2])
            if commit:
                self.rec.step(bits)
            else:
                self.m.step(bits)
            if not self.fighting():
                return False
        return True

    def decide(self):
        """prueba cada maniobra desde el estado actual y devuelve la mejor"""
        m = self.m
        st = m.save()
        base = self.value()
        facing = self.facing_right()
        x0 = abs(m.w(P1 + OFF_X) - m.w(P2 + OFF_X))
        best, best_v, best_f = None, None, facing
        bonus = self.bonus()
        cands = [(n, facing) for n in self.moves]
        if bonus:                                       # sin rival: probar hacia los dos lados
            cands += [(n, not facing) for n in self.moves]
        self.rng.shuffle(cands)
        for name, fc in cands:
            m.load(st)
            self.play(self.moves[name] + self.rollout, fc, commit=False)
            v = self.value() - base
            if not bonus:
                dist = abs(m.w(P1 + OFF_X) - m.w(P2 + OFF_X))
                v -= 0.002 * dist                      # a igualdad, acercarse
            v += self.rng.random() * 1e-4
            if best_v is None or v > best_v:
                best, best_v, best_f = name, v, fc
        m.load(st)
        self.decisions += 1
        return best, best_v, best_f

    def fight(self):
        """juega mientras dure el asalto; reintenta el asalto si lo pierde"""
        m = self.m
        start_state = m.save()
        start_frame = m.frame()
        tries = 0
        losing = self.args.lose_after is not None and m.b(PROGRESS) >= self.args.lose_after and not self.bonus()
        while True:
            while self.fighting():
                if losing:                                  # perder a propósito: quieto hasta el KO
                    self.play(seq(("N", 8, 0, 0)), True)
                    continue
                name, v, facing = self.decide()
                self.play(self.moves[name], facing)
            # fin del asalto (KO o tiempo): ¿lo ha perdido el jugador 1?  (en el bonus no se pierde)
            if self.bonus():
                return False
            lost = self.hp(P1) < 0 or (self.hp(P2) >= 0 and self.hp(P1) < self.hp(P2))
            if lost and tries < self.args.retries and not losing:
                tries += 1
                self.log("  asalto perdido en el cuadro %d (vida %d contra %d): repito (%d)" % (
                    m.frame(), self.hp(P1), self.hp(P2), tries))
                m.load(start_state)
                self.rec.truncate(start_frame)
                self.rng.seed(self.args.seed * 1000 + tries * 7 + start_frame)
                continue
            return lost


# ---- el jugador 2 humano (partidas a dos) ------------------------------------------------------------------------
class P2Policy:
    """jugador 2 sencillo y determinista: cada 24 cuadros una acción elegida por un hash de (semilla, tramo); la
    dirección depende del lado en que esté.  Es función pura del cuadro y del estado, así que las simulaciones de la
    búsqueda del jugador 1 ven exactamente lo que hará."""
    ACTIONS = ["avanza", "bloquea", "agachado", "salto", "lp", "mp", "hp", "lk", "mk", "hk", "agachado_hk", "qcf_hp"]

    def __init__(self, m, seed):
        self.m, self.seed = m, seed

    def bits(self, frame):
        m = self.m
        slot, ph = divmod(frame, 24)
        h = (slot * 2654435761 + self.seed * 40503) & 0xFFFFFFFF
        h ^= h >> 13
        act = self.ACTIONS[h % len(self.ACTIONS)]
        right = m.w(P2 + OFF_X) < m.w(P1 + OFF_X)
        f, b = (R, L) if right else (L, R)
        d = p = k = 0
        if act == "avanza":
            d = f
        elif act == "bloquea":
            d = b
        elif act == "agachado":
            d = D
        elif act == "salto":
            d = U | f if ph < 3 else 0
        elif act in ("lp", "mp", "hp"):
            p = {"lp": LP, "mp": MP, "hp": HP}[act] if ph < 3 else 0
        elif act in ("lk", "mk", "hk"):
            k = {"lk": LK, "mk": MK, "hk": HK}[act] if ph < 3 else 0
        elif act == "agachado_hk":
            d = D
            k = HK if ph < 3 else 0
        elif act == "qcf_hp":
            d = [D, D, D | f, D | f, f, f][ph] if ph < 6 else 0
            p = HP if 4 <= ph < 6 else 0
        return (0, (d | p) << 8, k << 4)


# ---- pantallas ------------------------------------------------------------------------------------------------
def run(args):
    lib = args.lib or os.path.join(ROOT, "build", "libsf2py.dylib")
    m = Machine(lib, args.roms, video=bool(args.png_dir))
    dips = [int(x, 16) for x in args.dips.split(",")]
    m.set_dips(*dips)
    rec = Recorder(m)
    t0 = time.time()

    def log(s):
        print("[%7.1fs f=%6d] %s" % (time.time() - t0, m.frame(), s), flush=True)

    pl = Player(m, rec, args, log)
    args.live = (m, rec, pl)
    nothing = (0, 0, 0)

    def pulse(key_bits, on=4, off=8):
        rec.hold(key_bits, on)
        rec.hold(nothing, off)

    def mode():
        return (m.b(0xFF8001), m.b(0xFF8005), m.b(0xFF8009))

    def snap(tag):
        if args.png_dir:
            m.png(os.path.join(args.png_dir, "%s_%06d.png" % (tag, m.frame())))

    if args.mode == "service":
        run_service(m, rec, args, log, pulse)
        return m, rec, pl

    targets = {1: CHARS.index(args.char), 2: CHARS.index(args.p2_char)}
    p2 = P2Policy(m, args.seed)
    pl.p2 = None                       # política del jugador 2 cuando es humano (partida a dos)

    if args.resume:
        # reanudar: reproducir lo grabado en un .partial (o en una secuencia) y seguir jugando desde ahí
        _, per = read_sequence(args.resume)
        head, _ = read_sequence(args.resume)
        upto = int(head["frames"])
        while m.frame() < upto:
            rec.step(tuple(per.get(m.frame() + 1, (0, 0, 0))))
        log("reanudado desde %s en el cuadro %d (modo %s)" % (args.resume, m.frame(), mode()))
        return play_loop(m, rec, pl, args, log, pulse, mode, snap, targets, p2, True)

    # arranque: test de RAM y aviso; monedas y start
    rec.hold(nothing, args.coin_at)
    two = args.mode == "versus"
    for _ in range(args.coins * (2 if two else 1)):
        pulse((IN0["coin1"], 0, 0), 6, 12)
    log("monedas: %d (créditos %d)" % (args.coins * (2 if two else 1), m.b(CREDITS)))
    start_key = IN0["start2"] if two else IN0["start1"]
    # start: el título con créditos es el modo 4 (con juego gratis vale la atracción, modo 2); al aceptarlo el modo
    # pasa a 0 y después a la selección (2, fase 0).  La atracción también pasa por 0 (tras el aviso, modo 0x10): solo
    # cuenta el paso a 0 desde 2 o 4 justo después de pulsar.
    started = False
    last_press = -100
    while not started and m.frame() < args.coin_at + 20000:
        before = m.b(0xFF8001)
        f = m.frame()
        if before in (2, 4) and f - last_press >= 40:
            rec.hold((start_key, 0, 0), 4)
            last_press = f
        else:
            rec.step(nothing)
        if before in (2, 4) and m.b(0xFF8001) == 0 and m.frame() - last_press <= 12:
            started = True
    if not started:
        raise SystemExit("playgen: el juego no aceptó el start")
    while not (m.b(0xFF8001) == 2 and m.b(0xFF8005) == 0) and m.frame() < args.max_frames:
        rec.step(nothing)
    log("start%s: modo %s, créditos %d" % ("2" if two else "1", mode(), m.b(CREDITS)))
    if two:
        pl.p2 = p2
    return play_loop(m, rec, pl, args, log, pulse, mode, snap, targets, p2, False)


def play_loop(m, rec, pl, args, log, pulse, mode, snap, targets, p2, resumed):
    nothing = (0, 0, 0)
    game_over_at = None
    in_game = resumed and m.b(0xFF8001) == 2
    last_checkpoint = m.frame()
    last_mode = None
    continues = args.continues
    challenged = False
    initials_done = False
    while m.frame() < args.max_frames:
        md = mode()
        if md[0] == 2 and md[1] == 4:
            in_game = True
        if in_game and md[0] != 2 and game_over_at is None:
            game_over_at = m.frame()
            log("fin de la partida (modo %02x): sigo %d cuadros más" % (md[0], args.tail))
        if game_over_at is not None and m.frame() >= game_over_at + args.tail:
            break
        if md != last_mode:
            log("modo %02x %02x %02x  combates %d rival %d vida %d/%d puntos %d/%d" % (
                md[0], md[1], md[2], m.b(PROGRESS), m.b(P2 + 6), pl.hp(P1), pl.hp(P2), pl.score(),
                m.bcd(P2 + OFF_SCORE)))
            snap("modo_%02x%02x%02x" % md)
            last_mode = md
        if md[0] == 2 and (md[1] == 0 or (md[1] == 0x0C and pl.p2 is not None)):
            # pantalla de selección: cada jugador humano mueve su cursor hasta su personaje y confirma.  En la fase 0x0C
            # (tras continuar o cuando entra un retador) el jugador que ya estaba no puede cambiar de personaje (SF2
            # World Warrior): solo elige el que entra
            sides = ([1] if md[1] == 0 else []) + ([2] if pl.p2 is not None else [])
            bits = [0, 0, 0]
            for side in sides:
                base = P1 if side == 1 else P2
                cur = m.b(base + OFF_CURSOR)
                tgt = targets[side]
                if cur != tgt:
                    cr, cc = divmod(cur, 4)
                    tr, tc = divmod(tgt, 4)
                    v = R if tc > cc else L if tc < cc else D if tr > cr else U
                else:
                    v = LP
                bits[1] |= v << (8 if side == 2 else 0)
            rec.hold(tuple(bits), 3)
            rec.hold(nothing, 20)
            continue
        if pl.fighting():
            if args.mode == "challenger" and not challenged and m.b(PROGRESS) >= args.challenge_at and not pl.bonus():
                # "HERE COMES A NEW CHALLENGER": el jugador 2 echa moneda y pulsa start a mitad del combate
                pulse((IN0["coin2"], 0, 0), 6, 12)
                pulse((IN0["start2"], 0, 0), 4, 20)
                challenged = True
                pl.p2 = p2
                log("  retador: moneda 2 y start 2 (créditos %d)" % m.b(CREDITS))
                continue
            lost = pl.fight()
            log("  asalto %s (vida %d contra %d, %d decisiones)" % ("perdido" if lost else "ganado",
                                                                    pl.hp(P1), pl.hp(P2), pl.decisions))
            if args.out and m.frame() - last_checkpoint > 3000:
                write_sequence(args.out + ".partial", args, rec, m, pl)     # punto de control (--resume)
                last_checkpoint = m.frame()
            if pl.p2 is not None and m.b(P2 + 6) == 0:
                pass
            continue
        if md[0] == 2 and md[1] == 0x0A and in_game and not initials_done:
            # fin de partida con récord: tabla con las iniciales (palanca para elegir la letra, puñetazo para fijarla)
            rec.hold(nothing, 60)
            for steps in (2, 5, 1):
                for _ in range(steps):
                    pulse(p1(R), 3, 12)
                pulse(p1(LP), 3, 24)
            initials_done = True
            log("  iniciales en la tabla de récords")
            continue
        if md[1] == 8 and md[2] == 6 and continues > 0:
            # CONTINUE? con la cuenta atrás: moneda y start
            continues -= 1
            pulse((IN0["coin1"], 0, 0), 6, 12)
            for _ in range(30):                     # el start solo vale cuando aparece "PUSH START"
                pulse((IN0["start1"], 0, 0), 4, 16)
                if m.b(0xFF8005) != 8:
                    break
            log("  continuar (quedan %d): modo %s" % (continues, mode()))
            pl.p2 = None
            continue
        rec.step(nothing)
    return m, rec, pl


def run_service(m, rec, args, log, pulse):
    """menú de pruebas (dipswitch "Game Mode" = Test), en lazo cerrado: 0xFF8001 = 2 en el menú y 4 dentro de una opción,
    0xFF8005 = 2 x opción (0 COLOR, 2 DIP SWITCH, 4 IO TEST, 6 DOT CLOTH, 8 CHARACTER) y, dentro de IO TEST, 0xFF800D =
    2 x subopción (0 INPUT, 2 OUTPUT, 4 SOUND).  A (puñetazo débil) entra; B (medio) sale de COLOR, DIP SWITCH, DOT CLOTH,
    CHARACTER y del submenú de E/S; A+B sale de INPUT, OUTPUT y SOUND.  La palanca no siempre responde justo después de
    salir de una pantalla, así que se entra, se mira dónde se ha entrado y, si no es lo buscado, se sale y se baja otra."""
    nothing = (0, 0, 0)
    A, B, AB = p1(LP), p1(MP), (0, LP | MP, 0)
    marks = []

    def mark(what):
        marks.append((m.frame(), what))
        log("  %s" % what)

    def where():
        return (m.b(0xFF8001), m.b(0xFF8005), m.b(0xFF800D))

    def every_input():
        for bits in [p1(R), p1(L), p1(U), p1(D), (0, 0x0100, 0), (0, 0x0200, 0), (0, 0x0400, 0), (0, 0x0800, 0),
                     p1(LP), p1(MP), p1(HP), (0, 0x1000, 0), (0, 0x2000, 0), (0, 0x4000, 0), p1(0, LK), p1(0, MK),
                     p1(0, HK), (0, 0, 0x10), (0, 0, 0x20), (0, 0, 0x40), (IN0["start1"], 0, 0), (IN0["start2"], 0, 0),
                     (IN0["coin1"], 0, 0), (IN0["coin2"], 0, 0), (IN0["service"], 0, 0)]:
            pulse(bits, 6, 18)

    def in_io_test():                        # dentro de INPUT, OUTPUT o SOUND
        return m.b(0xFF8001) == 4 and m.b(0xFF8005) == 4 and m.b(0xFF8009) == 4

    def in_io_menu():                        # en el submenú de IO TEST
        return m.b(0xFF8001) == 4 and m.b(0xFF8005) == 4 and m.b(0xFF8009) == 2

    def open_sub(k):
        """subopción k de IO TEST: al entrar desde el menú principal el cursor vuelve a INPUT, así que se baja k veces"""
        for attempt in range(8):
            if in_io_test():
                pulse(AB, 4, 120)
            if m.b(0xFF8001) == 2:
                pulse(A, 3, 120)             # el cursor del menú principal sigue en IO TEST
            if not in_io_menu():
                continue
            rec.hold(nothing, 60)
            for _ in range(k):
                pulse(p1(D), 3, 50)
            pulse(A, 3, 120)
            if in_io_test() and m.b(0xFF800D) == 2 * k:
                return True
        log("  no se pudo abrir la subopción %d de IO TEST (en %s)" % (k, where()))
        return False

    def open_item(k, sub=None):
        """desde el menú (o el submenú de E/S si sub) entra en la opción k; devuelve True si lo consigue"""
        if sub is not None:
            return open_sub(k)
        for _ in range(16):
            pulse(A, 3, 120)
            w = where()
            if sub is None and w[0] == 4 and w[1] == 2 * k and (k != 2 or in_io_menu()):
                return True
            if sub is not None and in_io_test() and w[2] == 2 * k:
                return True
            # dentro de otra: salir (A+B en las pruebas de E/S, B en el resto) y bajar una
            if in_io_test():
                pulse(AB, 4, 120)
            elif w[0] == 4 and sub is None:
                pulse(B, 3, 120)
            if sub is not None and m.b(0xFF8001) == 2:
                continue
            pulse(p1(D), 3, 60)
        log("  no se pudo abrir la opción %d%s (en %s)" % (k, "" if sub is None else " del submenú", where()))
        return False

    rec.hold(nothing, 900)
    mark("menú de pruebas (modo %02x %02x)" % (m.b(0xFF8001), m.b(0xFF8005)))
    if open_item(0):
        mark("1 COLOR")
        rec.hold(nothing, 120)
        pulse(B, 3, 120)
    if open_item(1):
        mark("2 DIP SWITCH")
        rec.hold(nothing, 120)
        pulse(B, 3, 120)
    if open_item(2):
        mark("3 IO TEST")
        if open_item(0, sub=True):
            mark("3.1 INPUT")
            every_input()
            pulse(AB, 4, 120)
        if open_item(1, sub=True):
            mark("3.2 OUTPUT")
            # contadores y bloqueo de monedas con A y B (sin 1P START, que voltea la pantalla: no implementado, D24)
            for bits in (A, A, B, B):
                pulse(bits, 4, 60)
            pulse(AB, 4, 120)
        if open_item(2, sub=True):
            mark("3.3 SOUND")
            for _ in range(6):                  # cambiar de código y hacerlo sonar
                pulse(p1(U), 3, 20)
                pulse(A, 3, 90)
            pulse(p1(D), 3, 20)
            pulse(A, 3, 90)
            pulse(AB, 4, 120)
        pulse(B, 3, 120)
        mark("menú (salida de IO TEST, modo %02x)" % m.b(0xFF8001))
    if open_item(3):
        mark("4 DOT CLOTH")
        rec.hold(nothing, 120)
        pulse(B, 3, 120)
    if open_item(4):
        mark("5 CHARACTER")
        for bits in [p1(R), p1(R), p1(D), p1(L), p1(U), A, p1(HP), p1(0, LK), p1(0, MK), p1(0, HK), p1(R), p1(R)]:
            pulse(bits, 4, 40)
        for _ in range(4):                      # el visor de caracteres tiene subpantallas: B hasta volver al menú
            pulse(B, 3, 120)
            if m.b(0xFF8001) == 2:
                break
        mark("menú (salida de CHARACTER, modo %02x)" % m.b(0xFF8001))
    rec.hold(nothing, 300)
    args.marks = marks


def write_sequence(path, args, rec, m, pl, extra_header=""):
    lines = rec.lines()
    with open(path, "w") as f:
        f.write("# %s\n" % args.title)
        f.write("# Generada con tools/playgen.py (semilla %d, horizonte %d, reintentos %d) sobre el núcleo recompilado.\n" % (
            args.seed, args.horizon, args.retries))
        f.write("# Formato: tools/sequences/README.md.  Cuadro N = N-ésima entrega de la IRQ2.\n")
        f.write("#@ frames %d\n" % m.frame())
        f.write("#@ dips %s\n" % args.dips.replace(",", " "))
        f.write("#@ state_crc %08x\n" % m.state_crc())
        f.write(extra_header)
        for fr, what in getattr(args, "marks", []):
            f.write("#@ mark %d %s\n" % (fr, what))
        for a, b, k in lines:
            f.write("%d %d %s\n" % (a, b, k))


# ---- comprobación y anotación de una secuencia ---------------------------------------------------------------
FIGHTERS = ["Ryu", "E. Honda", "Blanka", "Guile", "Ken", "Chun-Li", "Zangief", "Dhalsim", "M. Bison", "Sagat", "Balrog",
            "Vega"]


def read_sequence(path):
    """(cabecera, entradas por cuadro) de una secuencia"""
    head = {"dips": "ff fc 9f", "frames": None, "state_crc": None, "comments": [], "other": []}
    per = {}
    for line in open(path):
        if line.startswith("#@"):
            parts = line[2:].strip().split(None, 1)
            k = parts[0]
            v = parts[1].strip() if len(parts) > 1 else ""
            if k in ("frames", "dips", "state_crc"):
                head[k] = v
            elif k not in ("mark", "snap"):
                head["other"].append(line.rstrip("\n"))
            continue
        if line.startswith("#"):
            head["comments"].append(line.rstrip("\n"))
            continue
        body = line.split("#")[0].split()
        if len(body) != 3:
            continue
        a, b, key = int(body[0]), int(body[1]), body[2]
        port, mask = KEYS[key]
        for f in range(a, b + 1):
            v = per.setdefault(f, [0, 0, 0])
            v[port] |= mask
    return head, per


def verify(path, args, rewrite=True):
    """reproduce la secuencia con la biblioteca, comprueba el CRC del estado final y anota los hitos (#@ mark) y los
    cuadros de vídeo que conviene comparar con MAME (#@ snap)"""
    head, per = read_sequence(path)
    lib = args.lib or os.path.join(ROOT, "build", "libsf2py.dylib")
    m = Machine(lib, args.roms)
    m.set_dips(*[int(x, 16) for x in head["dips"].replace(",", " ").split()])
    frames = int(head["frames"])
    marks = []
    last = None
    in_game = False
    sel_from = None
    fights = 0
    while m.frame() < frames:
        f = m.frame() + 1
        m.step(tuple(per.get(f, (0, 0, 0))))
        md = (m.b(0xFF8001), m.b(0xFF8005), m.b(0xFF8009))
        if md == last:
            continue
        fr = m.frame()
        p2 = m.b(P2 + 6) != 0
        # la selección dura segundos; la atracción pasa por el mismo modo un solo cuadro
        if last is not None and last[0] == 2 and last[1] == 0 and md[:2] != (2, 0) and sel_from and fr - sel_from > 20:
            marks.append((sel_from, "selección de personaje"))
        if md[:2] == (2, 0) and (last is None or last[:2] != (2, 0)):
            sel_from = fr
        if md[0] == 2 and md[1] == 4 and md[2] == 0x0A:
            in_game = True
            if not p2:
                marks.append((fr, "fase de bonus"))
            else:
                c1, c2 = m.b(P1 + OFF_CURSOR), m.b(P2 + OFF_CURSOR)
                n1 = FIGHTERS[c1] if c1 < 12 else str(c1)
                n2 = FIGHTERS[c2] if c2 < 12 else str(c2)
                marks.append((fr, "asalto: %s contra %s" % (n1, n2)))
        elif md[0] == 2 and md[1] == 4 and md[2] == 0x0C and last and last[2] == 0x0A:
            marks.append((fr, "fin del asalto (vida %d contra %d)" % (m.sw(P1 + OFF_HP), m.sw(P2 + OFF_HP))))
        elif md[0] == 2 and md[1] == 8 and md[2] == 6:
            marks.append((fr, "CONTINUE? (cuenta atrás)"))
        elif md[0] == 2 and md[1] not in (0, 4, 8) and (last is None or last[1] != md[1]):
            name = {6: "final del personaje y créditos", 0x0A: "fin de partida (tabla de récords)",
                    0x0C: "selección bloqueada (continuar o retador)"}
            marks.append((fr, name.get(md[1], "fase %02x de la partida" % md[1]) if in_game else
                          "atracción: fase %02x" % md[1]))
        elif md[0] != 2 and in_game and last and last[0] == 2:
            marks.append((fr, "vuelta a la atracción (modo %02x)" % md[0]))
            in_game = False
        last = md
    test_mode = not (int(head["dips"].replace(",", " ").split()[2], 16) & 0x80)
    if test_mode:
        marks = []                       # menú de pruebas: los hitos los anota el generador (run_service)
    crc = "%08x" % m.state_crc()
    ok = head["state_crc"] is None or head["state_crc"] == crc
    print("%s: %d cuadros, CRC del estado %s %s" % (path, m.frame(), crc,
          "(igual al de la grabación)" if head["state_crc"] == crc else
          "(la grabación no lo anota)" if head["state_crc"] is None else "DISTINTO del grabado %s" % head["state_crc"]))
    for fr, what in marks:
        print("  %6d  %s" % (fr, what))
    if rewrite and not test_mode:
        snaps = sorted(set(fr + 30 for fr, _ in marks if fr + 30 <= frames))
        lines = [l for l in open(path) if not l.startswith("#@ mark") and not l.startswith("#@ snap")]
        out = []
        inserted = False
        for l in lines:
            if not l.startswith("#") and not inserted:
                for fr, what in marks:
                    out.append("#@ mark %d %s\n" % (fr, what))
                if snaps:
                    out.append("#@ snap %s\n" % ",".join(str(x) for x in snaps))
                inserted = True
            out.append(l)
        if not inserted:
            for fr, what in marks:
                out.append("#@ mark %d %s\n" % (fr, what))
        with open(path, "w") as fo:
            fo.writelines(out)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--roms", default=os.path.join(ROOT, "sf2"))
    ap.add_argument("--lib")
    ap.add_argument("--char", default="ryu", choices=CHARS)
    ap.add_argument("--dips", default="ff,fc,9f")
    ap.add_argument("--coins", type=int, default=1)
    ap.add_argument("--coin-at", type=int, default=300)
    ap.add_argument("--mode", default="arcade", choices=["arcade", "versus", "challenger", "service"])
    ap.add_argument("--p2-char", default="ken", choices=CHARS)
    ap.add_argument("--continues", type=int, default=0, help="veces que se continúa tras perder")
    ap.add_argument("--lose-after", type=int, help="tras ganar N combates, el jugador 1 se deja ganar")
    ap.add_argument("--challenge-at", type=int, default=1, help="modo challenger: combates ganados antes del reto")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--horizon", type=int, default=36)
    ap.add_argument("--retries", type=int, default=4)
    ap.add_argument("--max-frames", type=int, default=200000)
    ap.add_argument("--tail", type=int, default=1200, help="cuadros que se graban tras volver a la atracción")
    ap.add_argument("--png-dir")
    ap.add_argument("--title", default="Secuencia generada por tools/playgen.py")
    ap.add_argument("--out")
    ap.add_argument("--resume", metavar="SECUENCIA", help="reproducir esta secuencia (p. ej. un .partial) y seguir jugando")
    ap.add_argument("--verify", nargs="+", metavar="SECUENCIA",
                    help="reproduce las secuencias, comprueba #@ state_crc y anota hitos y cuadros de vídeo")
    args = ap.parse_args()
    if args.verify:
        bad = [p for p in args.verify if not verify(p, args)]
        if bad:
            raise SystemExit("no reproducen el estado grabado: %s" % ", ".join(bad))
        return
    if not args.out:
        ap.error("falta --out")
    if args.png_dir:
        os.makedirs(args.png_dir, exist_ok=True)
    try:
        m, rec, pl = run(args)
    except RuntimeError as e:
        # el núcleo se detuvo (p. ej. un salto a código que el recompilador no vio): guardar lo jugado hasta ahí para
        # reproducir el fallo con sf2_blind y para seguir con --resume tras corregir hints.txt y regenerar
        m, rec, pl = args.live
        write_sequence(args.out + ".partial", args, rec, m, pl)
        raise SystemExit("playgen: %s\n(partida hasta el cuadro %d en %s.partial)" % (e, m.frame(), args.out))
    if os.path.exists(args.out + ".partial"):
        os.remove(args.out + ".partial")
    write_sequence(args.out, args, rec, m, pl)
    print("secuencia: %s (%d líneas, %d cuadros)" % (args.out, len(rec.lines()), m.frame()))


if __name__ == "__main__":
    main()
