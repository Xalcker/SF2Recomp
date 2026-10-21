"""Decodificador completo del juego de instrucciones MC68000 (sin 68010/68020).

Produce objetos Insn con mnemónico, tamaño, operandos y longitud en bytes.  Lo usan
el desensamblador por descenso recursivo (analyze.py) y el generador de C (gen_c.py).
"""
from dataclasses import dataclass, field
from typing import List, Optional

# Clases de modos de direccionamiento efectivos.
DN, AN, IND, POSTINC, PREDEC, D16AN, D8ANXN, ABSW, ABSL, D16PC, D8PCXN, IMM = (
    "dn", "an", "ind", "postinc", "predec", "d16an", "d8anxn", "absw", "absl", "d16pc", "d8pcxn", "imm")

MEM_MODES = {IND, POSTINC, PREDEC, D16AN, D8ANXN, ABSW, ABSL, D16PC, D8PCXN}
CTRL_MODES = {IND, D16AN, D8ANXN, ABSW, ABSL, D16PC, D8PCXN}
ALTERABLE = {DN, AN, IND, POSTINC, PREDEC, D16AN, D8ANXN, ABSW, ABSL}
DATA_ALT = ALTERABLE - {AN}
MEM_ALT = ALTERABLE - {DN, AN}
CTRL_ALT = CTRL_MODES - {D16PC, D8PCXN}
DATA = MEM_MODES | {DN, IMM}
ALL = MEM_MODES | {DN, AN, IMM}

CC_NAMES = ["t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"]


@dataclass
class Op:
    kind: str                 # uno de los modos de arriba, o: sr, ccr, usp, reglist, imm (quick/vector/count)
    reg: int = 0
    disp: int = 0             # desplazamiento con signo (d16 / d8)
    ixreg: int = 0            # registro índice
    ixan: bool = False        # índice es An
    ixlong: bool = False      # índice .l (si no, .w con signo)
    val: int = 0              # inmediato / dirección absoluta / destino de salto / lista de regs
    pcbase: int = 0           # para d16pc/d8pcxn: dirección base (pc de la palabra de extensión)

    def abs_addr(self) -> Optional[int]:
        """Dirección efectiva estática si el operando la tiene (absw/absl/d16pc)."""
        if self.kind in (ABSW, ABSL):
            return self.val
        if self.kind == D16PC:
            return (self.pcbase + self.disp) & 0xFFFFFFFF
        return None

    def is_mem(self) -> bool:
        return self.kind in MEM_MODES

    def text(self, size: int = 2) -> str:
        k = self.kind
        if k == DN:
            return f"d{self.reg}"
        if k == AN:
            return f"a{self.reg}"
        if k == IND:
            return f"(a{self.reg})"
        if k == POSTINC:
            return f"(a{self.reg})+"
        if k == PREDEC:
            return f"-(a{self.reg})"
        if k == D16AN:
            return f"{self.disp:#x}(a{self.reg})" if self.disp >= 0 else f"-{-self.disp:#x}(a{self.reg})"
        if k == D8ANXN:
            ix = f"{'a' if self.ixan else 'd'}{self.ixreg}.{'l' if self.ixlong else 'w'}"
            return f"{self.disp:#x}(a{self.reg},{ix})" if self.disp >= 0 else f"-{-self.disp:#x}(a{self.reg},{ix})"
        if k == ABSW:
            return f"${self.val:04x}.w"
        if k == ABSL:
            return f"${self.val:06x}.l"
        if k == D16PC:
            return f"${(self.pcbase + self.disp) & 0xFFFFFF:06x}(pc)"
        if k == D8PCXN:
            ix = f"{'a' if self.ixan else 'd'}{self.ixreg}.{'l' if self.ixlong else 'w'}"
            return f"${(self.pcbase + self.disp) & 0xFFFFFF:06x}(pc,{ix})"
        if k == IMM:
            return f"#${self.val:x}"
        if k == "sr":
            return "sr"
        if k == "ccr":
            return "ccr"
        if k == "usp":
            return "usp"
        if k == "reglist":
            return reglist_text(self.val)
        if k == "target":
            return f"${self.val:06x}"
        return k


def reglist_text(mask: int) -> str:
    """Lista de registros en orden canónico d0-d7/a0-a7 (bit i = d_i, bit 8+i = a_i)."""
    parts = []
    for base, letter in ((0, "d"), (8, "a")):
        i = 0
        while i < 8:
            if mask & (1 << (base + i)):
                j = i
                while j + 1 < 8 and mask & (1 << (base + j + 1)):
                    j += 1
                parts.append(f"{letter}{i}" if i == j else f"{letter}{i}-{letter}{j}")
                i = j + 1
            else:
                i += 1
    return "/".join(parts) if parts else "(none)"


@dataclass
class Insn:
    addr: int
    length: int
    mnem: str                 # mnemónico base, p.ej. "move", "bcc", "dbcc", "jsr"
    size: int = 0             # 1, 2, 4 ó 0 si no aplica
    ops: List[Op] = field(default_factory=list)
    cc: int = 0               # condición para bcc/dbcc/scc
    words: List[int] = field(default_factory=list)

    @property
    def next(self) -> int:
        return self.addr + self.length

    def text(self) -> str:
        m = self.mnem
        if m in ("bcc", "dbcc", "scc"):
            m = {"bcc": "b", "dbcc": "db", "scc": "s"}[m] + CC_NAMES[self.cc]
            if self.mnem == "bcc" and self.cc == 0:
                m = "bra"
            if self.mnem == "dbcc" and self.cc == 1:
                m = "dbra"
        sfx = {1: ".b", 2: ".w", 4: ".l"}.get(self.size, "")
        if self.mnem in ("bcc", "bsr"):
            sfx = ".s" if self.length == 2 else ".w"
        if self.mnem in ("dbcc", "jsr", "jmp", "lea", "pea", "swap", "nop", "rts", "rte", "rtr", "trap", "illegal",
                         "link", "unlk", "reset", "stop", "trapv", "exg", "scc", "tas", "nbcd", "abcd", "sbcd",
                         "divu", "divs", "mulu", "muls", "chk", "bra", "movep"):
            sfx = sfx if self.mnem in ("movep", "chk", "divu", "divs", "mulu", "muls", "abcd", "sbcd", "nbcd") else ""
        if self.mnem in ("abcd", "sbcd", "nbcd", "tas", "scc"):
            sfx = ""
        if self.mnem in ("divu", "divs", "mulu", "muls", "chk"):
            sfx = ".w"
        ops = ",".join(o.text(self.size) for o in self.ops)
        return f"{m}{sfx} {ops}".rstrip()


class Decoder:
    """Decodifica a partir de una función rd16(addr) -> palabra de 16 bits (o None fuera de rango)."""

    def __init__(self, rd16):
        self.rd16 = rd16

    def word(self, a):
        w = self.rd16(a)
        if w is None:
            raise IndexError
        return w

    def decode(self, addr: int) -> Optional[Insn]:
        try:
            return self._decode(addr)
        except IndexError:
            return None

    # ---- utilidades ---------------------------------------------------------
    def _ea(self, mode: int, reg: int, size: int, pos: int, allowed: set):
        """Devuelve (Op, pos_siguiente) o lanza ValueError si el EA no está permitido."""
        if mode == 0:
            op = Op(DN, reg)
        elif mode == 1:
            op = Op(AN, reg)
        elif mode == 2:
            op = Op(IND, reg)
        elif mode == 3:
            op = Op(POSTINC, reg)
        elif mode == 4:
            op = Op(PREDEC, reg)
        elif mode == 5:
            d = self.word(pos)
            op = Op(D16AN, reg, disp=sext16(d))
            pos += 2
        elif mode == 6:
            ext = self.word(pos)
            op = Op(D8ANXN, reg, disp=sext8(ext & 0xFF), ixreg=(ext >> 12) & 7,
                    ixan=bool(ext & 0x8000), ixlong=bool(ext & 0x800))
            pos += 2
        elif mode == 7:
            if reg == 0:
                op = Op(ABSW, val=sext16(self.word(pos)) & 0xFFFFFFFF)
                pos += 2
            elif reg == 1:
                op = Op(ABSL, val=(self.word(pos) << 16) | self.word(pos + 2))
                pos += 4
            elif reg == 2:
                d = self.word(pos)
                op = Op(D16PC, disp=sext16(d), pcbase=pos)
                pos += 2
            elif reg == 3:
                ext = self.word(pos)
                op = Op(D8PCXN, disp=sext8(ext & 0xFF), ixreg=(ext >> 12) & 7,
                        ixan=bool(ext & 0x8000), ixlong=bool(ext & 0x800), pcbase=pos)
                pos += 2
            elif reg == 4:
                if size == 4:
                    op = Op(IMM, val=(self.word(pos) << 16) | self.word(pos + 2))
                    pos += 4
                elif size == 2:
                    op = Op(IMM, val=self.word(pos))
                    pos += 2
                elif size == 1:
                    op = Op(IMM, val=self.word(pos) & 0xFF)
                    pos += 2
                else:
                    raise ValueError("imm sin tamaño")
            else:
                raise ValueError("modo 7 reg>4")
        else:
            raise ValueError
        if op.kind not in allowed:
            raise ValueError(f"EA {op.kind} no permitido")
        if size == 1 and op.kind == AN:
            raise ValueError("An con tamaño byte")
        return op, pos

    def _imm(self, size: int, pos: int):
        return self._ea(7, 4, size, pos, {IMM})

    def _mk(self, addr, pos, mnem, size=0, ops=None, cc=0):
        words = [self.word(a) for a in range(addr, pos, 2)]
        return Insn(addr, pos - addr, mnem, size, ops or [], cc, words)

    # ---- decodificación principal -------------------------------------------
    def _decode(self, addr: int) -> Optional[Insn]:
        op = self.word(addr)
        try:
            return self._dispatch(addr, op)
        except ValueError:
            return None

    def _dispatch(self, addr, op):
        line = op >> 12
        if line == 0:
            return self._line0(addr, op)
        if line in (1, 2, 3):
            return self._move(addr, op)
        if line == 4:
            return self._line4(addr, op)
        if line == 5:
            return self._line5(addr, op)
        if line == 6:
            return self._line6(addr, op)
        if line == 7:
            if op & 0x100:
                raise ValueError
            return self._mk(addr, addr + 2, "moveq", 4, [Op(IMM, val=sext8(op & 0xFF) & 0xFFFFFFFF), Op(DN, (op >> 9) & 7)])
        if line == 8:
            return self._line8(addr, op)
        if line == 9:
            return self._addsub(addr, op, "sub")
        if line == 0xB:
            return self._lineB(addr, op)
        if line == 0xC:
            return self._lineC(addr, op)
        if line == 0xD:
            return self._addsub(addr, op, "add")
        if line == 0xE:
            return self._lineE(addr, op)
        raise ValueError("línea A/F")

    def _line0(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        if op & 0x100:
            dn = (op >> 9) & 7
            if mode == 1:  # MOVEP
                opmode = (op >> 6) & 3
                size = 2 if opmode in (0, 2) else 4
                d = sext16(self.word(addr + 2))
                mem = Op(D16AN, reg, disp=d)
                ops = [mem, Op(DN, dn)] if opmode < 2 else [Op(DN, dn), mem]
                return self._mk(addr, addr + 4, "movep", size, ops)
            kind = ["btst", "bchg", "bclr", "bset"][(op >> 6) & 3]
            allowed = DATA if kind == "btst" else DATA_ALT
            # btst Dn,#imm: el inmediato es un byte
            ea, pos = self._ea(mode, reg, 1, addr + 2, allowed)
            size = 4 if ea.kind == DN else 1
            return self._mk(addr, pos, kind, size, [Op(DN, dn), ea])
        kind = (op >> 9) & 7
        szbits = (op >> 6) & 3
        if kind == 4:  # bit estático
            bkind = ["btst", "bchg", "bclr", "bset"][szbits]
            bit = self.word(addr + 2) & 0xFF
            allowed = (DATA - {IMM}) if bkind == "btst" else DATA_ALT
            ea, pos = self._ea(mode, reg, 1, addr + 4, allowed)
            size = 4 if ea.kind == DN else 1
            return self._mk(addr, pos, bkind, size, [Op(IMM, val=bit), ea])
        if kind == 7:
            raise ValueError("MOVES/0x0Exx")
        if szbits == 3:
            raise ValueError
        size = (1, 2, 4)[szbits]
        mnem = ["ori", "andi", "subi", "addi", None, "eori", "cmpi"][kind]
        imm, pos = self._imm(size, addr + 2)
        if mode == 7 and reg == 4:  # a CCR / SR
            if mnem not in ("ori", "andi", "eori"):
                raise ValueError
            if size == 1:
                return self._mk(addr, pos, mnem, 1, [imm, Op("ccr")])
            if size == 2:
                return self._mk(addr, pos, mnem, 2, [imm, Op("sr")])
            raise ValueError
        allowed = DATA_ALT
        ea, pos = self._ea(mode, reg, size, pos, allowed)
        return self._mk(addr, pos, mnem, size, [imm, ea])

    def _move(self, addr, op):
        size = {1: 1, 3: 2, 2: 4}[op >> 12]
        smode, sreg = (op >> 3) & 7, op & 7
        dmode, dreg = (op >> 6) & 7, (op >> 9) & 7
        src, pos = self._ea(smode, sreg, size, addr + 2, ALL)
        if dmode == 1:
            if size == 1:
                raise ValueError
            dst, pos = self._ea(dmode, dreg, size, pos, {AN})
            return self._mk(addr, pos, "movea", size, [src, dst])
        dst, pos = self._ea(dmode, dreg, size, pos, DATA_ALT)
        return self._mk(addr, pos, "move", size, [src, dst])

    def _line4(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        if op & 0x100:
            an = (op >> 9) & 7
            opm = (op >> 6) & 3
            if opm == 3:
                ea, pos = self._ea(mode, reg, 4, addr + 2, CTRL_MODES)
                return self._mk(addr, pos, "lea", 4, [ea, Op(AN, an)])
            if opm == 2:
                ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
                return self._mk(addr, pos, "chk", 2, [ea, Op(DN, an)])
            raise ValueError
        hi = (op >> 8) & 0xF
        szbits = (op >> 6) & 3
        if hi == 0x0:
            if szbits == 3:
                ea, pos = self._ea(mode, reg, 2, addr + 2, DATA_ALT)
                return self._mk(addr, pos, "move", 2, [Op("sr"), ea])
            size = (1, 2, 4)[szbits]
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA_ALT)
            return self._mk(addr, pos, "negx", size, [ea])
        if hi == 0x2:
            if szbits == 3:
                raise ValueError("move from ccr es 68010+")
            size = (1, 2, 4)[szbits]
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA_ALT)
            return self._mk(addr, pos, "clr", size, [ea])
        if hi == 0x4:
            if szbits == 3:
                ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
                return self._mk(addr, pos, "move", 2, [ea, Op("ccr")])
            size = (1, 2, 4)[szbits]
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA_ALT)
            return self._mk(addr, pos, "neg", size, [ea])
        if hi == 0x6:
            if szbits == 3:
                ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
                return self._mk(addr, pos, "move", 2, [ea, Op("sr")])
            size = (1, 2, 4)[szbits]
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA_ALT)
            return self._mk(addr, pos, "not", size, [ea])
        if hi == 0x8:
            if szbits == 0:
                ea, pos = self._ea(mode, reg, 1, addr + 2, DATA_ALT)
                return self._mk(addr, pos, "nbcd", 1, [ea])
            if szbits == 1:
                if mode == 0:
                    return self._mk(addr, addr + 2, "swap", 4, [Op(DN, reg)])
                ea, pos = self._ea(mode, reg, 4, addr + 2, CTRL_MODES)
                return self._mk(addr, pos, "pea", 4, [ea])
            if mode == 0:
                return self._mk(addr, addr + 2, "ext", 2 if szbits == 2 else 4, [Op(DN, reg)])
            size = 2 if szbits == 2 else 4
            mask = self.word(addr + 2)
            ea, pos = self._ea(mode, reg, size, addr + 4, CTRL_ALT | {PREDEC})
            if ea.kind == PREDEC:
                mask = reverse16(mask)
            return self._mk(addr, pos, "movem", size, [Op("reglist", val=mask), ea])
        if hi == 0xA:
            if szbits == 3:
                if op == 0x4AFC:
                    return self._mk(addr, addr + 2, "illegal")
                ea, pos = self._ea(mode, reg, 1, addr + 2, DATA_ALT)
                return self._mk(addr, pos, "tas", 1, [ea])
            size = (1, 2, 4)[szbits]
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA_ALT)
            return self._mk(addr, pos, "tst", size, [ea])
        if hi == 0xC:
            if szbits < 2:
                raise ValueError("mull/divl 68020")
            size = 2 if szbits == 2 else 4
            mask = self.word(addr + 2)
            ea, pos = self._ea(mode, reg, size, addr + 4, CTRL_MODES | {POSTINC})
            return self._mk(addr, pos, "movem", size, [ea, Op("reglist", val=mask)])
        if hi == 0xE:
            if szbits == 1:
                sub = (op >> 3) & 7
                if sub in (0, 1):
                    return self._mk(addr, addr + 2, "trap", 0, [Op(IMM, val=op & 0xF)])
                if sub == 2:
                    d = sext16(self.word(addr + 2))
                    return self._mk(addr, addr + 4, "link", 0, [Op(AN, reg), Op(IMM, val=d & 0xFFFF)])
                if sub == 3:
                    return self._mk(addr, addr + 2, "unlk", 0, [Op(AN, reg)])
                if sub == 4:
                    return self._mk(addr, addr + 2, "move", 4, [Op(AN, reg), Op("usp")])
                if sub == 5:
                    return self._mk(addr, addr + 2, "move", 4, [Op("usp"), Op(AN, reg)])
                if sub == 6:
                    if reg == 0:
                        return self._mk(addr, addr + 2, "reset")
                    if reg == 1:
                        return self._mk(addr, addr + 2, "nop")
                    if reg == 2:
                        return self._mk(addr, addr + 4, "stop", 0, [Op(IMM, val=self.word(addr + 2))])
                    if reg == 3:
                        return self._mk(addr, addr + 2, "rte")
                    if reg == 5:
                        return self._mk(addr, addr + 2, "rts")
                    if reg == 6:
                        return self._mk(addr, addr + 2, "trapv")
                    if reg == 7:
                        return self._mk(addr, addr + 2, "rtr")
                raise ValueError
            if szbits == 2:
                ea, pos = self._ea(mode, reg, 4, addr + 2, CTRL_MODES)
                return self._mk(addr, pos, "jsr", 0, [ea])
            if szbits == 3:
                ea, pos = self._ea(mode, reg, 4, addr + 2, CTRL_MODES)
                return self._mk(addr, pos, "jmp", 0, [ea])
            raise ValueError
        raise ValueError

    def _line5(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        szbits = (op >> 6) & 3
        cc = (op >> 8) & 0xF
        if szbits == 3:
            if mode == 1:
                d = sext16(self.word(addr + 2))
                return self._mk(addr, addr + 4, "dbcc", 2, [Op(DN, reg), Op("target", val=(addr + 2 + d) & 0xFFFFFFFF)], cc)
            ea, pos = self._ea(mode, reg, 1, addr + 2, DATA_ALT)
            return self._mk(addr, pos, "scc", 1, [ea], cc)
        size = (1, 2, 4)[szbits]
        q = (op >> 9) & 7 or 8
        ea, pos = self._ea(mode, reg, size, addr + 2, ALTERABLE)
        return self._mk(addr, pos, "subq" if op & 0x100 else "addq", size, [Op(IMM, val=q), ea])

    def _line6(self, addr, op):
        cc = (op >> 8) & 0xF
        d8 = op & 0xFF
        if d8 == 0:
            d = sext16(self.word(addr + 2))
            pos = addr + 4
        elif d8 == 0xFF:
            raise ValueError("bcc.l es 68020")
        else:
            d = sext8(d8)
            pos = addr + 2
        target = (addr + 2 + d) & 0xFFFFFFFF
        if cc == 1:
            return self._mk(addr, pos, "bsr", 0, [Op("target", val=target)])
        return self._mk(addr, pos, "bcc", 0, [Op("target", val=target)], cc)

    def _line8(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        dn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm == 3:
            ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
            return self._mk(addr, pos, "divu", 2, [ea, Op(DN, dn)])
        if opm == 7:
            ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
            return self._mk(addr, pos, "divs", 2, [ea, Op(DN, dn)])
        size = (1, 2, 4)[opm & 3]
        if opm < 4:
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA)
            return self._mk(addr, pos, "or", size, [ea, Op(DN, dn)])
        if opm == 4 and mode in (0, 1):
            if mode == 0:
                return self._mk(addr, addr + 2, "sbcd", 1, [Op(DN, reg), Op(DN, dn)])
            return self._mk(addr, addr + 2, "sbcd", 1, [Op(PREDEC, reg), Op(PREDEC, dn)])
        if mode in (0, 1):
            raise ValueError("pack/unpk 68020")
        ea, pos = self._ea(mode, reg, size, addr + 2, MEM_ALT)
        return self._mk(addr, pos, "or", size, [Op(DN, dn), ea])

    def _addsub(self, addr, op, mnem):
        mode, reg = (op >> 3) & 7, op & 7
        rn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            size = 2 if opm == 3 else 4
            ea, pos = self._ea(mode, reg, size, addr + 2, ALL)
            return self._mk(addr, pos, mnem + "a", size, [ea, Op(AN, rn)])
        size = (1, 2, 4)[opm & 3]
        if opm < 4:
            ea, pos = self._ea(mode, reg, size, addr + 2, ALL)
            return self._mk(addr, pos, mnem, size, [ea, Op(DN, rn)])
        if mode == 0:
            return self._mk(addr, addr + 2, mnem + "x", size, [Op(DN, reg), Op(DN, rn)])
        if mode == 1:
            return self._mk(addr, addr + 2, mnem + "x", size, [Op(PREDEC, reg), Op(PREDEC, rn)])
        ea, pos = self._ea(mode, reg, size, addr + 2, MEM_ALT)
        return self._mk(addr, pos, mnem, size, [Op(DN, rn), ea])

    def _lineB(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        rn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            size = 2 if opm == 3 else 4
            ea, pos = self._ea(mode, reg, size, addr + 2, ALL)
            return self._mk(addr, pos, "cmpa", size, [ea, Op(AN, rn)])
        size = (1, 2, 4)[opm & 3]
        if opm < 4:
            ea, pos = self._ea(mode, reg, size, addr + 2, ALL)
            return self._mk(addr, pos, "cmp", size, [ea, Op(DN, rn)])
        if mode == 1:
            return self._mk(addr, addr + 2, "cmpm", size, [Op(POSTINC, reg), Op(POSTINC, rn)])
        ea, pos = self._ea(mode, reg, size, addr + 2, DATA_ALT)
        return self._mk(addr, pos, "eor", size, [Op(DN, rn), ea])

    def _lineC(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        dn = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm == 3:
            ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
            return self._mk(addr, pos, "mulu", 2, [ea, Op(DN, dn)])
        if opm == 7:
            ea, pos = self._ea(mode, reg, 2, addr + 2, DATA)
            return self._mk(addr, pos, "muls", 2, [ea, Op(DN, dn)])
        size = (1, 2, 4)[opm & 3]
        if opm < 4:
            ea, pos = self._ea(mode, reg, size, addr + 2, DATA)
            return self._mk(addr, pos, "and", size, [ea, Op(DN, dn)])
        if opm == 4 and mode == 0:
            return self._mk(addr, addr + 2, "abcd", 1, [Op(DN, reg), Op(DN, dn)])
        if opm == 4 and mode == 1:
            return self._mk(addr, addr + 2, "abcd", 1, [Op(PREDEC, reg), Op(PREDEC, dn)])
        if opm == 5 and mode == 0:
            return self._mk(addr, addr + 2, "exg", 4, [Op(DN, dn), Op(DN, reg)])
        if opm == 5 and mode == 1:
            return self._mk(addr, addr + 2, "exg", 4, [Op(AN, dn), Op(AN, reg)])
        if opm == 6 and mode == 1:
            return self._mk(addr, addr + 2, "exg", 4, [Op(DN, dn), Op(AN, reg)])
        if mode in (0, 1):
            raise ValueError
        ea, pos = self._ea(mode, reg, size, addr + 2, MEM_ALT)
        return self._mk(addr, pos, "and", size, [Op(DN, dn), ea])

    def _lineE(self, addr, op):
        mode, reg = (op >> 3) & 7, op & 7
        szbits = (op >> 6) & 3
        left = bool(op & 0x100)
        if szbits == 3:
            if op & 0x800:
                raise ValueError("bitfield 68020")
            kind = ["as", "ls", "rox", "ro"][(op >> 9) & 3]
            ea, pos = self._ea(mode, reg, 2, addr + 2, MEM_ALT)
            return self._mk(addr, pos, kind + ("l" if left else "r"), 2, [ea])
        size = (1, 2, 4)[szbits]
        kind = ["as", "ls", "rox", "ro"][(op >> 3) & 3]
        cnt = (op >> 9) & 7
        if op & 0x20:
            cop = Op(DN, cnt)
        else:
            cop = Op(IMM, val=cnt or 8)
        return self._mk(addr, addr + 2, kind + ("l" if left else "r"), size, [cop, Op(DN, reg)])


def sext8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def sext16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def reverse16(m):
    r = 0
    for i in range(16):
        if m & (1 << i):
            r |= 1 << (15 - i)
    return r


class RomReader:
    def __init__(self, data: bytes, base: int = 0):
        self.data = data
        self.base = base

    def rd16(self, a):
        a &= 0xFFFFFF
        o = a - self.base
        if o < 0 or o + 2 > len(self.data) or a & 1:
            return None
        return (self.data[o] << 8) | self.data[o + 1]

    def rd32(self, a):
        hi, lo = self.rd16(a), self.rd16(a + 2)
        if hi is None or lo is None:
            return None
        return (hi << 16) | lo


if __name__ == "__main__":
    import sys
    rom = open(sys.argv[1], "rb").read()
    start = int(sys.argv[2], 16)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    rr = RomReader(rom)
    dec = Decoder(rr.rd16)
    a = start
    for _ in range(n):
        ins = dec.decode(a)
        if ins is None:
            print(f"{a:06x}: {rr.rd16(a):04x}  ???")
            a += 2
            continue
        ws = " ".join(f"{w:04x}" for w in ins.words)
        print(f"{a:06x}: {ws:<30} {ins.text()}")
        a = ins.next
