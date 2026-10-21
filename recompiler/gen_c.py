#!/usr/bin/env python3
"""Generador de C a partir del análisis del ROM 68000.

Una función C por función 68000 (`void f_XXXXXX(M68K *c)`).  Cada función empieza con un
`switch (c->pc)` que salta a la etiqueta de cualquier inicio de bloque, de modo que la tabla
de despacho dirección->función permite entrar a mitad de función (retornos inesperados,
cambios de tarea, punteros a código en RAM).

Convenciones de flujo de control:
  jsr/bsr X   -> push32(ret); c->pc = X; f_X(c); if (c->pc != ret) return;
  rts         -> c->pc = pop32(); return;            (el llamador comprueba la dirección)
  jmp X       -> goto (misma función) | tail call a f_X | despacho por tabla
  jmp (ea)    -> switch sobre los destinos conocidos; por defecto despacho por tabla
  trap/chk/div0/privilegio -> exception(); dispatch_call(); if (c->pc != ret) return;
"""
import argparse
import zlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from analyze import analyze, Analysis
from m68k import (Insn, Op, DN, AN, IND, POSTINC, PREDEC, D16AN, D8ANXN, ABSW, ABSL, D16PC, D8PCXN, IMM,
                  MEM_MODES)

BITS = {1: 8, 2: 16, 4: 32}
MASK = {1: 0xff, 2: 0xffff, 4: 0xffffffff}

# Ciclos totales de cada excepción en el 68000 según Musashi (m68ki_exception_cycle_table[0]); la
# excepción sustituye al coste base de la instrucción que la provoca.  Los demás ajustes dependientes
# de los datos (Bcc no tomado, DBcc, desplazamientos, MOVEM, Scc, MULU/MULS) se emiten junto a cada
# instrucción para que el reloj del modo autónomo coincida con el de Musashi.
EXC_CYCLES = {4: 34, 5: 38, 6: 40, 7: 34, 8: 34}
for _v in range(32, 48):
    EXC_CYCLES[_v] = 34
COND = {0: "1", 1: "0", 2: "(!c->c && !c->z)", 3: "(c->c || c->z)", 4: "(!c->c)", 5: "(c->c)",
        6: "(!c->z)", 7: "(c->z)", 8: "(!c->v)", 9: "(c->v)", 10: "(!c->n)", 11: "(c->n)",
        12: "(c->n == c->v)", 13: "(c->n != c->v)", 14: "(!c->z && c->n == c->v)", 15: "(c->z || c->n != c->v)"}


def hx(v):
    return "0x%xu" % (v & 0xffffffff)


def sdisp(v):
    return "%d" % v if v < 0 else "%d" % v


class FuncCtx:
    def __init__(self, entry, body, labels):
        self.entry = entry
        self.body = body
        self.labels = labels


def mame_fix(ins: Insn) -> int:
    """Ciclos que el 68000 de MAME 0.289 (y el manual de Motorola) cuesta de más o de menos que la tabla de Musashi en
    las instrucciones de coste fijo (hito 6, D47; medido comparando trazas con el ciclo de cada instrucción):
    las operaciones .l con destino Dn (o An) y origen registro cuestan 8, no 6 ("add 2 for register direct or immediate":
    Musashi ya suma esos 2 con inmediato, no con registro), y andi.l #,Dn cuesta 16, no 14 (ori y eori ya valen 16)."""
    m, sz, ops = ins.mnem, ins.size, ins.ops
    if sz == 4 and m in ("add", "sub", "and", "or") and len(ops) == 2 and ops[1].kind == DN and ops[0].kind in (DN, AN):
        return 2
    if sz == 4 and m in ("adda", "suba") and ops[0].kind in (DN, AN):
        return 2
    if sz == 4 and m == "andi" and len(ops) == 2 and ops[1].kind == DN:
        return 2
    if sz == 2 and m == "addq" and ops[1].kind == AN:
        return 4                    # addq.w #,An es una suma de 32 bits: 8 ciclos como subq.w (Musashi pone 4)
    return 0


# ---- punto de muestreo de las interrupciones (D47) -----------------------------------------------------------
# Cada instrucción deja en c->ke un código de 16 bits que el gancho de la siguiente (runtime/machine.c) usa para saber hasta
# qué ciclo tenía que estar afirmada una IRQ para atenderse tras ella: K = fin del último acceso al bus anterior al último
# "latch" de interrupción del microcódigo del 68000 de MAME; E = fin del último acceso de la instrucción
# (recompiler/m68000_ack.txt, de tools/mame_m68000_ack.py).
#   bits 14-15  tipo de K: 0 ninguno (vale el último acceso anterior), 1 comienzo+k, 2 final-k;  bits 8-13  k
#   bits 6-7    tipo de E: 0 final, 1 comienzo+e, 2 ninguno;  bits 0-5  e
KE_K_NONE, KE_K_START, KE_K_END = 0, 1, 2
KE_E_END, KE_E_START, KE_E_NONE = 0, 1, 2


def ke_code(kk, kv, ek, ev):
    assert 0 <= kv < 64 and 0 <= ev < 64
    return (kk << 14) | (kv << 8) | (ek << 6) | ev


def load_ack(path):
    rows = []
    if not path or not os.path.exists(path):
        return rows
    for line in open(path):
        if line.startswith("#") or "|" not in line:
            continue
        head, ps = line.split("|")
        val, mask, name = head.split()
        paths = [tuple(None if x == "-" else int(x) for x in p.split("/")) for p in ps.split()]
        rows.append((int(val, 16), int(mask, 16), name, paths))
    return rows


def classify_paths(paths):
    """código ke para un conjunto de caminos con el mismo comportamiento, o None si no hay uno común"""
    Ks = {K for t, K, E in paths}
    Es = {E for t, K, E in paths}
    if Ks == {None}:
        kk, kv = KE_K_NONE, 0
    elif None not in Ks and len(Ks) == 1:
        kk, kv = KE_K_START, next(iter(Ks))
    elif None not in Ks and len({t - K for t, K, E in paths}) == 1:
        kk, kv = KE_K_END, next(iter({t - K for t, K, E in paths}))
    else:
        return None
    if Es == {None}:
        ek, ev = KE_E_NONE, 0
    elif None not in Es and {t - E for t, K, E in paths} == {0}:
        ek, ev = KE_E_END, 0
    elif None not in Es and len(Es) == 1:
        ek, ev = KE_E_START, next(iter(Es))
    else:
        return None
    return ke_code(kk, kv, ek, ev)


class Gen:
    def __init__(self, an: Analysis, cycles=None, clock="mame", ack=None, mods=None):
        self.an = an
        self.cycles = cycles
        self.mods = dict(mods or {})     # parches de lógica de recompiler/mod_patches.txt
        self.mods_used = set()
        self.clock = clock          # "mame": el reloj del 68000 de MAME 0.289 (D47); "musashi": el de los hitos 1-5 (D16)
        self.ack = ack or []
        self.ack_cache = {}
        self.tmp = 0

    def ke(self, ins: Insn):
        """(código por omisión, código del camino alternativo) de la instrucción: el alternativo es el de Bcc.b tomado y
        Scc a registro con condición cierta (sin latch previo en el otro camino); None si no hay"""
        w = ins.words[0]
        if w in self.ack_cache:
            return self.ack_cache[w]
        res = (0, None)
        for val, mask, name, paths in self.ack:
            if w & mask == val:
                one = classify_paths(paths)
                if one is not None:
                    res = (one, None)
                else:
                    none = [p for p in paths if p[1] is None]
                    some = [p for p in paths if p[1] is not None]
                    a, b = classify_paths(none), classify_paths(some)
                    if a is None or b is None:
                        raise SystemExit("gen_c.py: caminos de interrupción sin clasificar para %04x (%s): %s" % (w, name, paths))
                    res = (a, b)
                break
        self.ack_cache[w] = res
        return res

    # ---- utilidades --------------------------------------------------------------------
    def cyc(self, ins: Insn) -> int:
        if self.cycles is None:
            return 4
        base = self.cycles[ins.words[0]] or 4
        if self.clock == "mame":
            base += mame_fix(ins)
        return base

    def label(self, a):
        return "L_%06X" % a

    def fname(self, a):
        return "f_%06X" % a

    def goto(self, t, ctx: FuncCtx) -> str:
        an = self.an
        if t in ctx.body:
            return "goto %s;" % self.label(t)
        if t in an.insns and t in an.entries:
            return "{ c->pc = %s; __attribute__((musttail)) return %s(c); }" % (hx(t), self.fname(t))
        if t in an.insns:
            return "{ c->pc = %s; TAILDISPATCH; }" % hx(t)
        return 'rt_fatal(c, "salto a direccion no decodificada %%06x desde %%06x", %s, c->pc);' % hx(t)

    def call(self, t, ret, expect=None) -> str:
        """llamada: apila `ret` y, al volver, sigue si el PC es `expect` (por omisión `ret`; distinto tras una rutina
        de texto en línea, que vuelve detrás del texto)"""
        expect = ret if expect is None else expect
        if t in self.an.insns:
            fn = self.fname(t) if t in self.an.entries else None
            if fn:
                return "push32(c, %s); c->pc = %s; %s(c); if (c->pc != %s) return;" % (hx(ret), hx(t), fn, hx(expect))
            return "push32(c, %s); c->pc = %s; dispatch_call(c); if (c->pc != %s) return;" % (hx(ret), hx(t), hx(expect))
        return 'rt_fatal(c, "llamada a direccion no decodificada %%06x", %s);' % hx(t)

    def successor(self, ins: Insn) -> int:
        return self.an.inline_ret.get(ins.addr, ins.next)

    def exc(self, ins: Insn, vec, push_pc, ret) -> str:
        """Entra en la excepción `vec`, ejecuta el manejador anidado y sigue en `ret`."""
        return "CYC(%d); exception(c, %d, %s); dispatch_call(c); if (c->pc != %s) return;" % (
            EXC_CYCLES.get(vec, 4) - self.cyc(ins), vec, hx(push_pc), hx(ret))

    def priv(self, ins: Insn) -> str:
        return "if (!c->s) { CYC(%d); exception(c, 8, %s); dispatch_call(c); return; }" % (
            EXC_CYCLES[8] - self.cyc(ins), hx(ins.addr))

    # ---- operandos ---------------------------------------------------------------------
    def idx_expr(self, op: Op) -> str:
        r = "c->a[%d]" % op.ixreg if op.ixan else "c->d[%d]" % op.ixreg
        if op.ixlong:
            return r
        return "sext16(%s)" % r

    def ea_setup(self, op: Op, size: int, var: str):
        """Devuelve código C que define `uint32_t var` con la dirección efectiva (y aplica (An)+/-(An))."""
        k = op.kind
        if k == IND:
            return "uint32_t %s = c->a[%d];" % (var, op.reg)
        if k == POSTINC:
            inc = size if not (size == 1 and op.reg == 7) else 2
            return "uint32_t %s = c->a[%d]; c->a[%d] += %d;" % (var, op.reg, op.reg, inc)
        if k == PREDEC:
            inc = size if not (size == 1 and op.reg == 7) else 2
            return "c->a[%d] -= %d; uint32_t %s = c->a[%d];" % (op.reg, inc, var, op.reg)
        if k == D16AN:
            return "uint32_t %s = c->a[%d] + (uint32_t)(%s);" % (var, op.reg, sdisp(op.disp))
        if k == D8ANXN:
            return "uint32_t %s = c->a[%d] + (uint32_t)(%s) + %s;" % (var, op.reg, sdisp(op.disp), self.idx_expr(op))
        if k in (ABSW, ABSL):
            return "uint32_t %s = %s;" % (var, hx(op.val))
        if k == D16PC:
            return "uint32_t %s = %s;" % (var, hx(op.pcbase + op.disp))
        if k == D8PCXN:
            return "uint32_t %s = %s + %s;" % (var, hx(op.pcbase + op.disp), self.idx_expr(op))
        raise ValueError(k)

    def read(self, op: Op, size: int, var: str) -> str:
        k = op.kind
        if k == DN:
            return "c->d[%d]" % op.reg if size == 4 else "(c->d[%d] & %s)" % (op.reg, hx(MASK[size]))
        if k == AN:
            return "c->a[%d]" % op.reg if size == 4 else "(c->a[%d] & 0xffffu)" % op.reg
        if k == IMM:
            return hx(op.val & MASK[size])
        return "rd%d(c, %s)" % (BITS[size], var)

    def write(self, op: Op, size: int, var: str, val: str) -> str:
        k = op.kind
        if k == DN:
            return "setd%d(c, %d, %s);" % (BITS[size], op.reg, val)
        if k == AN:
            return "c->a[%d] = %s;" % (op.reg, val)
        return "wr%d(c, %s, %s);" % (BITS[size], var, val)

    # ---- instrucciones -----------------------------------------------------------------
    def emit(self, ins: Insn, ctx: FuncCtx) -> str:
        m = ins.mnem
        fn = getattr(self, "i_" + m, None)
        if fn is None:
            return 'rt_fatal(c, "instruccion no soportada %s en %%06x", c->pc);' % m
        return fn(ins, ctx)

    # -- transferencias
    def i_move(self, ins, ctx):
        src, dst = ins.ops
        sz = ins.size
        if dst.kind == "sr":
            return "%s { %s uint32_t v = %s; set_sr(c, v); }" % (
                self.priv(ins), self.ea_setup(src, 2, "ea0") if src.is_mem() else "", self.read(src, 2, "ea0"))
        if dst.kind == "ccr":
            return "{ %s uint32_t v = %s; set_ccr(c, v); }" % (
                self.ea_setup(src, 2, "ea0") if src.is_mem() else "", self.read(src, 2, "ea0"))
        if src.kind == "sr":
            return "{ %s uint32_t v = get_sr(c); %s }" % (
                self.ea_setup(dst, 2, "ea1") if dst.is_mem() else "", self.write(dst, 2, "ea1", "v"))
        if dst.kind == "usp":
            return "%s c->sp_other = c->a[%d];" % (self.priv(ins), src.reg)
        if src.kind == "usp":
            return "%s c->a[%d] = c->sp_other;" % (self.priv(ins), dst.reg)
        parts = []
        if src.is_mem():
            parts.append(self.ea_setup(src, sz, "ea0"))
        parts.append("uint32_t v = %s;" % self.read(src, sz, "ea0"))
        if dst.is_mem():
            parts.append(self.ea_setup(dst, sz, "ea1"))
        parts.append("op_logic%d(c, v);" % BITS[sz])
        parts.append(self.write(dst, sz, "ea1", "v"))
        return "{ %s }" % " ".join(parts)

    def i_movea(self, ins, ctx):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        if src.is_mem():
            parts.append(self.ea_setup(src, sz, "ea0"))
        v = self.read(src, sz, "ea0")
        if sz == 2:
            v = "sext16(%s)" % v
        parts.append("c->a[%d] = %s;" % (dst.reg, v))
        return "{ %s }" % " ".join(parts)

    def i_moveq(self, ins, ctx):
        return "c->d[%d] = %s; op_logic32(c, %s);" % (ins.ops[1].reg, hx(ins.ops[0].val), hx(ins.ops[0].val))

    def i_lea(self, ins, ctx):
        return "{ %s c->a[%d] = ea0; }" % (self.ea_setup(ins.ops[0], 4, "ea0"), ins.ops[1].reg)

    def i_pea(self, ins, ctx):
        return "{ %s push32(c, ea0); }" % self.ea_setup(ins.ops[0], 4, "ea0")

    def i_exg(self, ins, ctx):
        a, b = ins.ops
        ra = "c->%s[%d]" % ("d" if a.kind == DN else "a", a.reg)
        rb = "c->%s[%d]" % ("d" if b.kind == DN else "a", b.reg)
        return "{ uint32_t t = %s; %s = %s; %s = t; }" % (ra, ra, rb, rb)

    def i_swap(self, ins, ctx):
        r = ins.ops[0].reg
        return "c->d[%d] = (c->d[%d] >> 16) | (c->d[%d] << 16); op_logic32(c, c->d[%d]);" % (r, r, r, r)

    def i_ext(self, ins, ctx):
        r = ins.ops[0].reg
        if ins.size == 2:
            return "setd16(c, %d, sext8(c->d[%d])); op_logic16(c, c->d[%d]);" % (r, r, r)
        return "c->d[%d] = sext16(c->d[%d]); op_logic32(c, c->d[%d]);" % (r, r, r)

    def i_movem(self, ins, ctx):
        sz = ins.size
        nregs = bin(ins.ops[0].val if ins.ops[0].kind == "reglist" else ins.ops[1].val).count("1")
        if ins.ops[0].kind == "reglist":
            mask, ea = ins.ops[0].val, ins.ops[1]
            parts = ["CYC(%d);" % (nregs * (8 if sz == 4 else 4))]
            if ea.kind == PREDEC:
                parts.append("uint32_t ea0 = c->a[%d];" % ea.reg)
                for i in range(15, -1, -1):
                    if mask & (1 << i):
                        reg = "c->d[%d]" % i if i < 8 else "c->a[%d]" % (i - 8)
                        parts.append("ea0 -= %d; wr%d(c, ea0, %s);" % (sz, BITS[sz], reg))
                parts.append("c->a[%d] = ea0;" % ea.reg)
            else:
                parts.append(self.ea_setup(ea, sz, "ea0"))
                for i in range(16):
                    if mask & (1 << i):
                        reg = "c->d[%d]" % i if i < 8 else "c->a[%d]" % (i - 8)
                        parts.append("wr%d(c, ea0, %s); ea0 += %d;" % (BITS[sz], reg, sz))
            return "{ %s }" % " ".join(parts)
        ea, mask = ins.ops[0], ins.ops[1].val
        parts = ["CYC(%d);" % (nregs * (8 if sz == 4 else 4)),
                 self.ea_setup(ea, sz, "ea0") if ea.kind != POSTINC else "uint32_t ea0 = c->a[%d];" % ea.reg]
        for i in range(16):
            if mask & (1 << i):
                reg = "c->d[%d]" % i if i < 8 else "c->a[%d]" % (i - 8)
                val = "rd32(c, ea0)" if sz == 4 else "sext16(rd16(c, ea0))"
                parts.append("%s = %s; ea0 += %d;" % (reg, val, sz))
        if ea.kind == POSTINC:
            parts.append("c->a[%d] = ea0;" % ea.reg)
        return "{ %s }" % " ".join(parts)

    def i_movep(self, ins, ctx):
        a, b = ins.ops
        sz = ins.size
        if a.kind == D16AN:   # memoria -> registro
            parts = [self.ea_setup(a, sz, "ea0")]
            if sz == 2:
                parts.append("setd16(c, %d, (rd8(c, ea0) << 8) | rd8(c, ea0 + 2));" % b.reg)
            else:
                parts.append("c->d[%d] = (rd8(c, ea0) << 24) | (rd8(c, ea0 + 2) << 16) | (rd8(c, ea0 + 4) << 8) | rd8(c, ea0 + 6);" % b.reg)
            return "{ %s }" % " ".join(parts)
        parts = [self.ea_setup(b, sz, "ea0"), "uint32_t v = c->d[%d];" % a.reg]
        if sz == 2:
            parts.append("wr8(c, ea0, v >> 8); wr8(c, ea0 + 2, v & 0xff);")
        else:
            parts.append("wr8(c, ea0, v >> 24); wr8(c, ea0 + 2, (v >> 16) & 0xff); wr8(c, ea0 + 4, (v >> 8) & 0xff); wr8(c, ea0 + 6, v & 0xff);")
        return "{ %s }" % " ".join(parts)

    def i_link(self, ins, ctx):
        an, d = ins.ops
        disp = d.val - 0x10000 if d.val & 0x8000 else d.val
        if an.reg == 7:
            return "c->a[7] -= 4; wr32(c, c->a[7], c->a[7]); c->a[7] += (uint32_t)(%d);" % disp
        return "push32(c, c->a[%d]); c->a[%d] = c->a[7]; c->a[7] += (uint32_t)(%d);" % (an.reg, an.reg, disp)

    def i_unlk(self, ins, ctx):
        r = ins.ops[0].reg
        if r == 7:
            return "c->a[7] = rd32(c, c->a[7]);"
        return "c->a[7] = c->a[%d]; c->a[%d] = pop32(c);" % (r, r)

    # -- aritmética
    def _rmw(self, ins, dst, size, compute, read_dst=True, write_back=True):
        """Lee dst (con setup de EA), aplica `compute(d) -> expr`, y escribe el resultado."""
        parts = []
        if dst.is_mem():
            parts.append(self.ea_setup(dst, size, "ea1"))
        if read_dst:
            parts.append("uint32_t d = %s;" % self.read(dst, size, "ea1"))
        parts.append("uint32_t r = %s;" % compute)
        if write_back:
            parts.append(self.write(dst, size, "ea1", "r"))
        else:
            parts.append("(void)r;")
        return parts

    def _src(self, src, size, parts):
        if src.is_mem():
            parts.append(self.ea_setup(src, size, "ea0"))
        parts.append("uint32_t s = %s;" % self.read(src, size, "ea0"))

    def _binop(self, ins, opname, write_back=True):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        self._src(src, sz, parts)
        parts += self._rmw(ins, dst, sz, "op_%s%d(c, s, d)" % (opname, BITS[sz]), write_back=write_back)
        return "{ %s }" % " ".join(parts)

    def i_add(self, ins, ctx):
        return self._binop(ins, "add")

    def i_sub(self, ins, ctx):
        return self._binop(ins, "sub")

    def i_addi(self, ins, ctx):
        return self._binop(ins, "add")

    def i_subi(self, ins, ctx):
        return self._binop(ins, "sub")

    def i_cmp(self, ins, ctx):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        self._src(src, sz, parts)
        if dst.is_mem():
            parts.append(self.ea_setup(dst, sz, "ea1"))
        parts.append("uint32_t d = %s; op_cmp%d(c, s, d);" % (self.read(dst, sz, "ea1"), BITS[sz]))
        return "{ %s }" % " ".join(parts)

    i_cmpi = i_cmp

    def i_cmpm(self, ins, ctx):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        self._src(src, sz, parts)
        parts.append(self.ea_setup(dst, sz, "ea1"))
        parts.append("uint32_t d = rd%d(c, ea1); op_cmp%d(c, s, d);" % (BITS[sz], BITS[sz]))
        return "{ %s }" % " ".join(parts)

    def _addsub_a(self, ins, sign):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        self._src(src, sz, parts)
        s = "sext16(s)" if sz == 2 else "s"
        parts.append("c->a[%d] = c->a[%d] %s %s;" % (dst.reg, dst.reg, sign, s))
        return "{ %s }" % " ".join(parts)

    def i_adda(self, ins, ctx):
        return self._addsub_a(ins, "+")

    def i_suba(self, ins, ctx):
        return self._addsub_a(ins, "-")

    def i_cmpa(self, ins, ctx):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        self._src(src, sz, parts)
        s = "sext16(s)" if sz == 2 else "s"
        parts.append("op_cmp32(c, %s, c->a[%d]);" % (s, dst.reg))
        return "{ %s }" % " ".join(parts)

    def _quick(self, ins, sign):
        q, dst = ins.ops
        sz = ins.size
        if dst.kind == AN:
            return "c->a[%d] %s= %d;" % (dst.reg, sign, q.val)
        opname = "add" if sign == "+" else "sub"
        parts = ["uint32_t s = %d;" % q.val]
        parts += self._rmw(ins, dst, sz, "op_%s%d(c, s, d)" % (opname, BITS[sz]))
        return "{ %s }" % " ".join(parts)

    def i_addq(self, ins, ctx):
        return self._quick(ins, "+")

    def i_subq(self, ins, ctx):
        return self._quick(ins, "-")

    def _xop(self, ins, opname):
        src, dst = ins.ops
        sz = ins.size
        parts = []
        if src.kind == DN:
            parts.append("uint32_t s = %s;" % self.read(src, sz, ""))
            parts.append("uint32_t d = %s;" % self.read(dst, sz, ""))
            parts.append("uint32_t r = op_%s%d(c, s, d);" % (opname, BITS[sz]))
            parts.append(self.write(dst, sz, "", "r"))
        else:   # -(Ay),-(Ax): primero la fuente
            parts.append(self.ea_setup(src, sz, "ea0"))
            parts.append("uint32_t s = rd%d(c, ea0);" % BITS[sz])
            parts.append(self.ea_setup(dst, sz, "ea1"))
            parts.append("uint32_t d = rd%d(c, ea1);" % BITS[sz])
            parts.append("uint32_t r = op_%s%d(c, s, d);" % (opname, BITS[sz]))
            parts.append("wr%d(c, ea1, r);" % BITS[sz])
        return "{ %s }" % " ".join(parts)

    def i_addx(self, ins, ctx):
        return self._xop(ins, "addx")

    def i_subx(self, ins, ctx):
        return self._xop(ins, "subx")

    def _bcd(self, ins, opname):
        src, dst = ins.ops
        parts = []
        if src.kind == DN:
            parts.append("uint32_t s = c->d[%d] & 0xff; uint32_t d = c->d[%d] & 0xff;" % (src.reg, dst.reg))
            parts.append("setd8(c, %d, op_%s(c, s, d));" % (dst.reg, opname))
        else:
            parts.append(self.ea_setup(src, 1, "ea0"))
            parts.append("uint32_t s = rd8(c, ea0);")
            parts.append(self.ea_setup(dst, 1, "ea1"))
            parts.append("uint32_t d = rd8(c, ea1);")
            parts.append("wr8(c, ea1, op_%s(c, s, d));" % opname)
        return "{ %s }" % " ".join(parts)

    def i_abcd(self, ins, ctx):
        return self._bcd(ins, "abcd")

    def i_sbcd(self, ins, ctx):
        return self._bcd(ins, "sbcd")

    def i_nbcd(self, ins, ctx):
        dst = ins.ops[0]
        parts = self._rmw(ins, dst, 1, "op_nbcd(c, d)")
        return "{ %s }" % " ".join(parts)

    def _unary(self, ins, compute):
        dst = ins.ops[0]
        sz = ins.size
        parts = self._rmw(ins, dst, sz, compute % BITS[sz])
        return "{ %s }" % " ".join(parts)

    def i_neg(self, ins, ctx):
        return self._unary(ins, "op_neg%d(c, d)")

    def i_negx(self, ins, ctx):
        return self._unary(ins, "op_negx%d(c, d)")

    def i_not(self, ins, ctx):
        return self._unary(ins, "op_logic%d(c, ~d)")

    def i_clr(self, ins, ctx):
        dst = ins.ops[0]
        sz = ins.size
        parts = []
        if dst.is_mem():
            parts.append(self.ea_setup(dst, sz, "ea1"))
        parts.append("op_logic%d(c, 0);" % BITS[sz])
        parts.append(self.write(dst, sz, "ea1", "0"))
        return "{ %s }" % " ".join(parts)

    def i_tst(self, ins, ctx):
        dst = ins.ops[0]
        sz = ins.size
        parts = []
        if dst.is_mem():
            parts.append(self.ea_setup(dst, sz, "ea1"))
        parts.append("op_logic%d(c, %s);" % (BITS[sz], self.read(dst, sz, "ea1")))
        return "{ %s }" % " ".join(parts)

    def i_tas(self, ins, ctx):
        dst = ins.ops[0]
        parts = self._rmw(ins, dst, 1, "op_logic8(c, d) | 0x80")
        return "{ %s }" % " ".join(parts)

    def _logic(self, ins, cop):
        src, dst = ins.ops
        sz = ins.size
        if dst.kind == "sr":
            return "%s set_sr(c, get_sr(c) %s %s);" % (self.priv(ins), cop, hx(src.val))
        if dst.kind == "ccr":
            return "set_ccr(c, get_ccr(c) %s %s);" % (cop, hx(src.val))
        parts = []
        self._src(src, sz, parts)
        parts += self._rmw(ins, dst, sz, "op_logic%d(c, d %s s)" % (BITS[sz], cop))
        return "{ %s }" % " ".join(parts)

    def i_and(self, ins, ctx):
        return self._logic(ins, "&")

    def i_or(self, ins, ctx):
        return self._logic(ins, "|")

    def i_eor(self, ins, ctx):
        return self._logic(ins, "^")

    i_andi = i_and
    i_ori = i_or
    i_eori = i_eor

    def i_mulu(self, ins, ctx):
        return self._mul(ins, "mulu")

    def i_muls(self, ins, ctx):
        return self._mul(ins, "muls")

    def _mul(self, ins, opname):
        src, dst = ins.ops
        parts = []
        self._src(src, 2, parts)
        cyc = "cyc_muls_68k" if (opname == "muls" and self.clock == "mame") else "cyc_" + opname
        parts.append("CYC(%s(s));" % cyc)
        parts.append("c->d[%d] = op_%s(c, s, c->d[%d]);" % (dst.reg, opname, dst.reg))
        return "{ %s }" % " ".join(parts)

    def _div(self, ins, opname):
        src, dst = ins.ops
        parts = []
        self._src(src, 2, parts)
        parts.append("if (s == 0) { %s } else c->d[%d] = op_%s(c, c->d[%d], s);" % (
            self.exc(ins, 5, ins.next, ins.next), dst.reg, opname, dst.reg))
        return "{ %s }" % " ".join(parts)

    def i_divu(self, ins, ctx):
        return self._div(ins, "divu")

    def i_divs(self, ins, ctx):
        return self._div(ins, "divs")

    def i_chk(self, ins, ctx):
        src, dst = ins.ops
        parts = []
        self._src(src, 2, parts)
        parts.append("if (op_chk(c, c->d[%d], s)) { %s }" % (dst.reg, self.exc(ins, 6, ins.next, ins.next)))
        return "{ %s }" % " ".join(parts)

    # -- bits
    def _bit(self, ins, kind):
        bitop, dst = ins.ops
        parts = []
        if bitop.kind == DN:
            parts.append("uint32_t b = c->d[%d] & %d;" % (bitop.reg, 31 if dst.kind == DN else 7))
        else:
            parts.append("uint32_t b = %d;" % (bitop.val & (31 if dst.kind == DN else 7)))
        parts.append("uint32_t mask = 1u << b;")
        if dst.kind == DN and kind != "btst" and self.clock == "mame":
            # BSET/BCLR/BCHG sobre Dn: los ciclos de la tabla son el máximo (bit >= 16); con un bit < 16 son 2 menos
            parts.append("if (b < 16) CYC(-2);")
        if dst.kind == DN:
            parts.append("uint32_t d = c->d[%d];" % dst.reg)
        else:
            parts.append(self.ea_setup(dst, 1, "ea1"))
            parts.append("uint32_t d = rd8(c, ea1);")
        parts.append("c->z = (d & mask) == 0;")
        if kind != "btst":
            newv = {"bchg": "d ^ mask", "bclr": "d & ~mask", "bset": "d | mask"}[kind]
            if dst.kind == DN:
                parts.append("c->d[%d] = %s;" % (dst.reg, newv))
            else:
                parts.append("wr8(c, ea1, %s);" % newv)
        return "{ %s }" % " ".join(parts)

    def i_btst(self, ins, ctx):
        return self._bit(ins, "btst")

    def i_bchg(self, ins, ctx):
        return self._bit(ins, "bchg")

    def i_bclr(self, ins, ctx):
        return self._bit(ins, "bclr")

    def i_bset(self, ins, ctx):
        return self._bit(ins, "bset")

    # -- desplazamientos
    def _shift(self, ins, opname):
        sz = ins.size
        if len(ins.ops) == 1:   # forma de memoria, palabra, cuenta 1
            dst = ins.ops[0]
            parts = self._rmw(ins, dst, 2, "op_%s(c, 16, d, 1)" % opname)
            return "{ %s }" % " ".join(parts)
        cnt, dst = ins.ops
        count = "%d" % cnt.val if cnt.kind == IMM else "(c->d[%d] & 63)" % cnt.reg
        # Musashi: 2 ciclos por posición desplazada (cuenta original, también en ROX)
        return "{ uint32_t n = %s; CYC(2 * n); setd%d(c, %d, op_%s(c, %d, c->d[%d], n)); }" % (
            count, BITS[sz], dst.reg, opname, BITS[sz], dst.reg)

    def i_asl(self, ins, ctx): return self._shift(ins, "asl")
    def i_asr(self, ins, ctx): return self._shift(ins, "asr")
    def i_lsl(self, ins, ctx): return self._shift(ins, "lsl")
    def i_lsr(self, ins, ctx): return self._shift(ins, "lsr")
    def i_rol(self, ins, ctx): return self._shift(ins, "rol")
    def i_ror(self, ins, ctx): return self._shift(ins, "ror")
    def i_roxl(self, ins, ctx): return self._shift(ins, "roxl")
    def i_roxr(self, ins, ctx): return self._shift(ins, "roxr")

    # -- flujo de control
    def i_bcc(self, ins, ctx):
        t = ins.ops[0].val
        if ins.cc == 0:
            return self.goto(t, ctx)
        # Musashi: Bcc.b no tomado -2 ciclos, Bcc.w no tomado +2 (la tabla base es la del salto tomado)
        alt = self.ke(ins)[1] if self.clock == "mame" else None
        taken = ("c->ke = 0x%04xu; " % alt) if alt is not None else ""
        return "if %s { %s%s } CYC(%d);" % (COND[ins.cc], taken, self.goto(t, ctx), -2 if ins.length == 2 else 2)

    def i_bsr(self, ins, ctx):
        return self.call(ins.ops[0].val, ins.next, self.successor(ins))

    def i_dbcc(self, ins, ctx):
        r = ins.ops[0].reg
        t = ins.ops[1].val
        # Musashi: contador no agotado (salto) -2 ciclos, agotado +2; condición cierta: solo la base
        body = "uint32_t w = (c->d[%d] - 1) & 0xffffu; c->d[%d] = (c->d[%d] & 0xffff0000u) | w; if (w != 0xffffu) { CYC(-2); %s } CYC(2);" % (
            r, r, r, self.goto(t, ctx))
        if ins.cc == 1:
            return "{ %s }" % body
        return "if (!%s) { %s }" % (COND[ins.cc], body)

    def i_scc(self, ins, ctx):
        dst = ins.ops[0]
        parts = []
        if dst.is_mem():
            parts.append(self.ea_setup(dst, 1, "ea1"))
        parts.append("uint32_t v = %s ? 0xffu : 0u;" % COND[ins.cc])
        if not dst.is_mem():
            alt = self.ke(ins)[1] if self.clock == "mame" else None
            # Musashi: Scc a registro con condición cierta +2
            parts.append("if (v) { CYC(2); %s}" % (("c->ke = 0x%04xu; " % alt) if alt is not None else ""))
        parts.append(self.write(dst, 1, "ea1", "v"))
        return "{ %s }" % " ".join(parts)

    def i_jmp(self, ins, ctx):
        ea = ins.ops[0]
        t = ea.abs_addr()
        if t is not None:
            return self.goto(t & 0xffffff, ctx)
        targets = sorted(self.an.jumps.get(ins.addr, ()))
        parts = [self.ea_setup(ea, 4, "t"), "t &= 0xffffffu; c->pc = t;"]
        cases = []
        for tg in targets:
            cases.append("case %s: %s" % (hx(tg), self.goto(tg, ctx)))
        parts.append("switch (t) { %s default: TAILDISPATCH; }" % " ".join(cases))
        return "{ %s }" % " ".join(parts)

    def i_jsr(self, ins, ctx):
        ea = ins.ops[0]
        t = ea.abs_addr()
        if t is not None:
            return self.call(t & 0xffffff, ins.next, self.successor(ins))
        return "{ %s t &= 0xffffffu; push32(c, %s); c->pc = t; dispatch_call(c); if (c->pc != %s) return; }" % (
            self.ea_setup(ea, 4, "t"), hx(ins.next), hx(ins.next))

    def i_rts(self, ins, ctx):
        return "c->pc = pop32(c); return;"

    def i_rtr(self, ins, ctx):
        return "set_ccr(c, pop16(c)); c->pc = pop32(c); return;"

    def i_rte(self, ins, ctx):
        return "%s { uint32_t sr = pop16(c); c->pc = pop32(c); set_sr(c, sr); return; }" % self.priv(ins)

    def i_trap(self, ins, ctx):
        return "{ %s }" % self.exc(ins, 32 + ins.ops[0].val, ins.next, ins.next)

    def i_trapv(self, ins, ctx):
        return "if (c->v) { %s }" % self.exc(ins, 7, ins.next, ins.next)

    def i_illegal(self, ins, ctx):
        return "{ CYC(%d); exception(c, 4, %s); dispatch_call(c); return; }" % (EXC_CYCLES[4] - self.cyc(ins), hx(ins.addr))

    def i_nop(self, ins, ctx):
        return ""

    def i_reset(self, ins, ctx):
        return self.priv(ins)

    def i_stop(self, ins, ctx):
        return "%s set_sr(c, %s); c->stopped = 1; rt_stopped(c);" % (self.priv(ins), hx(ins.ops[0].val))

    # ---- funciones ---------------------------------------------------------------------
    def emit_function(self, entry: int) -> str:
        an = self.an
        body = an.funcs[entry]
        labels = {a for a in body if a in an.labels} | {entry}
        # cualquier destino de goto dentro del cuerpo necesita etiqueta, sea o no despachable
        addrs = sorted(body)
        nxt = {addrs[i]: addrs[i + 1] for i in range(len(addrs) - 1)}
        for a in addrs:
            ins = an.insns[a]
            for t in an.edges(ins):
                if t in body and (t != ins.next or nxt.get(a) != t):
                    labels.add(t)
            if self.falls_through(ins) and self.successor(ins) in body and nxt.get(a) != self.successor(ins):
                labels.add(self.successor(ins))
        ctx = FuncCtx(entry, body, labels)
        out = []
        out.append("void %s(M68K *c)\n{" % self.fname(entry))
        out.append("    switch (c->pc) {")
        for a in sorted(labels):
            if a in an.labels or a == entry:
                out.append("    case %s: goto %s;" % (hx(a), self.label(a)))
        out.append('    default: rt_fatal(c, "entrada invalida %%06x en %s", c->pc);' % self.fname(entry))
        out.append("    }")
        # emitir las instrucciones en orden de dirección; las roturas de secuencia necesitan goto
        prev_next = None
        for a in addrs:
            ins = an.insns[a]
            if prev_next is not None and prev_next != a:
                # el bloque anterior cayó hacia una dirección que no es `a`: saltar a ella explícitamente
                out.append("    " + self.goto(prev_next, ctx))
            if a in labels:
                out.append("%s:" % self.label(a))
            code = self.emit(ins, ctx)
            for old, new in self.mods.get(a, ()):
                if code.count(old) != 1:
                    raise SystemExit("mod_patches.txt: %s aparece %d veces en el C de %06x: %s" % (old, code.count(old), a, code))
                code = code.replace(old, new)
                self.mods_used.add(a)
            ke = ""
            if self.clock == "mame":
                kcode, alt = self.ke(ins)
                if ins.mnem == "bcc" and ins.cc == 0 and alt is not None:
                    kcode = alt                 # bra.s: siempre el camino tomado del manejador de Bcc.b
                ke = "c->ke = 0x%04xu; " % kcode
            out.append("    INSN(%s); %sCYC(%d); /* %s */" % (hx(a), ke, self.cyc(ins), ins.text().replace("*/", "* /")))
            if code:
                out.append("    " + code)
            prev_next = self.successor(ins) if self.falls_through(ins) else None
        if prev_next is not None:
            out.append("    " + self.goto(prev_next, ctx))
        out.append("}\n")
        return "\n".join(out)

    @staticmethod
    def falls_through(ins: Insn) -> bool:
        if ins.mnem in ("rts", "rte", "rtr", "jmp", "illegal"):
            return False
        if ins.mnem == "bcc" and ins.cc == 0:
            return False
        return True


def load_cycles(path):
    if not path or not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    if len(data) != 0x10000:
        return None
    return data


def load_mod_patches(path):
    """{direccion: [(viejo, nuevo), ...]} de recompiler/mod_patches.txt (formato en ese archivo)."""
    patches = {}
    if not path or not os.path.exists(path):
        return patches
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 3:
            raise SystemExit("%s:%d: se esperaba DIRECCION | viejo | nuevo" % (path, n))
        patches.setdefault(int(parts[0], 16), []).append((parts[1], parts[2]))
    return patches


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("outdir")
    ap.add_argument("--hints", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "hints.txt"))
    ap.add_argument("--cycles", help="tabla de ciclos base por opcode (65536 bytes, de dump_cycles)")
    ap.add_argument("--parts", type=int, default=16, help="número de archivos .c")
    ap.add_argument("--report", help="archivo de informe de cobertura")
    ap.add_argument("--mods", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "mod_patches.txt"),
                    help="parches de los mods de lógica (D58); todos valen lo mismo que el original con c->mod_wide = 0")
    ap.add_argument("--ack", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "m68000_ack.txt"),
                    help="punto de muestreo de interrupciones por instrucción (tools/mame_m68000_ack.py)")
    ap.add_argument("--clock", choices=["mame", "musashi"], default="mame",
                    help="modelo de ciclos: el del 68000 de MAME 0.289 (por omisión, D47) o el de Musashi (D16)")
    args = ap.parse_args()

    an = analyze(args.rom, args.hints)
    if args.report:
        with open(args.report, "w") as f:
            f.write(an.report() + "\n")
    ack = load_ack(args.ack) if args.clock == "mame" else []
    if args.clock == "mame" and not ack:
        raise SystemExit("gen_c.py: --clock mame necesita la tabla --ack (recompiler/m68000_ack.txt)")
    mods = load_mod_patches(args.mods)
    gen = Gen(an, load_cycles(args.cycles), args.clock, ack, mods)
    os.makedirs(args.outdir, exist_ok=True)

    entries = sorted(e for e in an.entries if e in an.funcs)
    # cabecera con prototipos
    with open(os.path.join(args.outdir, "recomp_funcs.h"), "w") as f:
        f.write("/* generado por gen_c.py - no editar */\n#ifndef RECOMP_FUNCS_H\n#define RECOMP_FUNCS_H\n")
        f.write('#include "cpu.h"\n')
        f.write("#define TAILDISPATCH do { RecompFn f_ = dispatch_fn(c); __attribute__((musttail)) return f_(c); } while (0)\n")
        for e in entries:
            f.write("void f_%06X(M68K *c);\n" % e)
        f.write("typedef struct DispatchEntry { uint32_t addr; RecompFn fn; } DispatchEntry;\n")
        f.write("extern const DispatchEntry recomp_table[];\nextern const unsigned recomp_table_len;\n")
        f.write("extern const uint32_t recomp_reset_pc;\nextern const unsigned recomp_insn_count;\n")
        f.write("extern const int recomp_clock_mame;\n")
        f.write("/* CRC-32 de la imagen de programa de 1 MiB con la que se generó este código (lo comprueba el juego al cargar las ROMs) */\n")
        f.write("extern const uint32_t recomp_rom_crc32;\n#endif\n")

    # reparto de funciones en N archivos equilibrados por tamaño de cuerpo
    parts = [[] for _ in range(args.parts)]
    sizes = [0] * args.parts
    for e in sorted(entries, key=lambda e: -len(an.funcs[e])):
        i = sizes.index(min(sizes))
        parts[i].append(e)
        sizes[i] += len(an.funcs[e])
    total_lines = 0
    for i, chunk in enumerate(parts):
        path = os.path.join(args.outdir, "recomp_%02d.c" % i)
        with open(path, "w") as f:
            f.write('/* generado por gen_c.py - no editar */\n#include "recomp_funcs.h"\n\n')
            for e in sorted(chunk):
                txt = gen.emit_function(e)
                total_lines += txt.count("\n")
                f.write(txt + "\n")

    # tabla de despacho: cada etiqueta -> función propietaria
    rows = []
    for a in sorted(an.labels):
        owner = an.owner.get(a)
        if owner is None:
            continue
        rows.append((a, owner))
    with open(os.path.join(args.outdir, "recomp_table.c"), "w") as f:
        f.write('/* generado por gen_c.py - no editar */\n#include "recomp_funcs.h"\n\n')
        f.write("const DispatchEntry recomp_table[] = {\n")
        for a, owner in rows:
            f.write("    { %s, f_%06X },\n" % (hx(a), owner))
        f.write("};\nconst unsigned recomp_table_len = %d;\n" % len(rows))
        f.write("const uint32_t recomp_reset_pc = %s;\n" % hx(an.rr.rd32(4)))
        f.write("const unsigned recomp_insn_count = %d;\n" % len(an.insns))
        f.write("const int recomp_clock_mame = %d;\n" % (1 if args.clock == "mame" else 0))
        with open(args.rom, "rb") as rf:
            f.write("const uint32_t recomp_rom_crc32 = 0x%08xu;\n" % (zlib.crc32(rf.read()) & 0xffffffff))

    missing = sorted(set(gen.mods) - gen.mods_used)
    if missing:
        raise SystemExit("mod_patches.txt: ninguna instrucción emitida en %s" % ", ".join("%06x" % a for a in missing))
    if gen.mods_used:
        print("parches de mods aplicados en %d instrucciones" % len(gen.mods_used))
    dup = sum(len(b) for b in an.funcs.values()) - len(set().union(*an.funcs.values()))
    print("funciones: %d  instrucciones: %d (duplicadas %d)  etiquetas despachables: %d  lineas C: %d" % (
        len(entries), len(an.insns), dup, len(rows), total_lines))


if __name__ == "__main__":
    main()
