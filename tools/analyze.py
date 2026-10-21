#!/usr/bin/env python3
"""Desensamblado por descenso recursivo del programa 68000 de SF2 (CPS1).

Parte de la tabla de vectores, sigue jsr/bsr/jmp/bcc/bra/dbcc y resuelve los idiomas
de tabla de salto PC-relativa de Capcom:

    add.w d0,d0 ; move.w tabla(pc,d0.w),d0 ; jmp/jsr tabla(pc,d0.w)      (tabla de offsets .w)
    movea.l tabla(pc,d0.w),a0 ; jmp/jsr (a0)                           (tabla de punteros .l)
    lea tabla(pc),a0 ; move.w (a0,d0.w),d0 ; jmp (a0,d0.w)              (idem vía An)
    jmp tabla(pc,d0.w) con tabla = secuencia de bra/jmp de paso fijo    (tabla de código)
    lea ret(pc),a4 ; bra sub  ...  jmp (a4)                             (retorno por registro)

Emite un informe de cobertura (bytes de código / tabla / desconocidos) y la lista de
saltos indirectos no resueltos.  Importable desde gen_c.py.
"""
import sys
import os
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68k import (Decoder, Insn, Op, RomReader, sext16, DN, AN, IND, POSTINC, PREDEC, D16AN, D8ANXN, ABSW, ABSL,
                  D16PC, D8PCXN, IMM, CC_NAMES)

ROM_END = 0x100000   # 1 MiB: cuatro pares de ROMs (el ultimo, 0xC0000-0xFFFFF, parece solo datos)
UNKNOWN, CODE, TABLE, VECTOR = 0, 1, 2, 3
KIND_NAMES = {UNKNOWN: "desconocido", CODE: "codigo", TABLE: "tabla", VECTOR: "vectores"}

TERMINATORS = {"rts", "rte", "rtr", "jmp", "illegal"}


# Valores simbólicos del evaluador hacia atrás -------------------------------------------
@dataclass(frozen=True)
class Const:
    val: int


@dataclass(frozen=True)
class TableWord:      # Dn = palabra leída de tabla[base + idx]
    base: int


@dataclass(frozen=True)
class TableLong:      # An = long leído de tabla[base + idx]
    base: int


@dataclass(frozen=True)
class Index:          # Dn es un índice calculado (byte/word, *2, etc.)
    pass


UNKNOWN_VAL = None


@dataclass
class Table:
    base: int               # dirección donde empiezan las entradas
    kind: str               # "word" | "long" | "code"
    rel: int                # base de los offsets (tablas word)
    targets: List[int] = field(default_factory=list)
    end: int = 0            # primera dirección tras la tabla
    users: Set[int] = field(default_factory=set)   # direcciones de los jmp/jsr que la usan
    is_call: bool = False


@dataclass
class Indirect:
    addr: int
    text: str
    info: str
    is_call: bool
    resolved: bool
    targets: List[int] = field(default_factory=list)
    table: Optional[int] = None


class Analysis:
    def __init__(self, rom: bytes, hints: Optional[dict] = None, verbose=False):
        self.rom = rom
        self.rr = RomReader(rom)
        self.dec = Decoder(self.rr.rd16)
        self.hints = hints or {}
        self.verbose = verbose
        self.hard_limits: Set[int] = set()     # direcciones que ninguna tabla puede atravesar
        self.table_end_override: Dict[int, int] = dict(self.hints.get("table_end", {}))
        self.reset()

    def reset(self):
        self.kind = bytearray(len(self.rom))
        self.insns: Dict[int, Insn] = {}
        self.prev: Dict[int, int] = {}            # insn -> insn anterior en el barrido lineal
        self.entries: Set[int] = set()            # entradas de función (destinos de llamada / vectores / punteros)
        self.entry_src: Dict[int, str] = {}
        self.labels: Set[int] = set()             # inicios de bloque (para la tabla de despacho)
        self.label_src: Dict[int, str] = {}       # el primer motivo de cada etiqueta (informes)
        self.label_srcs: Dict[int, Set[str]] = defaultdict(set)   # todos los motivos (límites de las tablas)
        self.tables: Dict[int, Table] = {}
        self.indirects: Dict[int, Indirect] = {}
        self.calls: Dict[int, Set[int]] = defaultdict(set)      # insn jsr -> destinos
        self.jumps: Dict[int, Set[int]] = defaultdict(set)      # insn jmp -> destinos
        self.worklist: List[Tuple[int, str]] = []
        self.pending_lea_ret: Dict[int, Tuple[int, int]] = {}   # addr destino -> (addr lea, An)
        self.code_ptrs: Set[int] = set()
        self.bad_decode: List[Tuple[int, int]] = []             # (addr, opcode) donde falló el decodificador
        self.overlaps: List[int] = []                           # barridos que chocaron con código ya decodificado
        self.inline_ret: Dict[int, int] = {}                    # llamada a rutina de texto en línea -> vuelta real

    # ---- utilidades ---------------------------------------------------------------------
    def in_rom(self, a):
        return 0 <= a < ROM_END

    def is_code_addr(self, a):
        return self.in_rom(a) and (a & 1) == 0

    def add_entry(self, a, src):
        if not self.is_code_addr(a):
            return
        if a not in self.entries:
            self.entries.add(a)
            self.entry_src[a] = src
        self.add_label(a, src)

    def add_label(self, a, src):
        if not self.is_code_addr(a):
            return
        self.label_srcs[a].add(src)
        if a not in self.labels:
            self.labels.add(a)
            self.label_src[a] = src
        if a not in self.insns:
            self.worklist.append((a, src))

    # ---- barrido lineal desde una dirección ---------------------------------------------
    def sweep(self, start):
        a = start
        prev = None
        while True:
            if a in self.insns:
                if prev is not None and a not in self.prev:
                    self.prev[a] = prev
                return
            if not self.is_code_addr(a):
                return
            if self.kind[a] == TABLE:
                return
            if self.kind[a] == CODE:
                # a mitad de una instrucción ya decodificada: flujos solapados (casi siempre datos
                # tomados por código); se registra y se detiene este barrido
                self.overlaps.append(a)
                return
            ins = self.dec.decode(a)
            if ins is None or ins.next > ROM_END:
                self.bad_decode.append((a, self.rr.rd16(a) or 0))
                return
            self.insns[a] = ins
            if prev is not None:
                self.prev[a] = prev
            for i in range(a, ins.next):
                self.kind[i] = CODE
            cont = self.follow(ins)
            prev = a
            a = ins.next
            if not cont:
                return

    def follow(self, ins: Insn) -> bool:
        """Registra sucesores; devuelve True si continúa al siguiente."""
        m = ins.mnem
        if m == "bcc":
            t = ins.ops[0].val
            self.add_label(t, "bcc")
            if ins.cc == 0:
                self.check_lea_ret(ins)
                return False
            return True
        if m == "dbcc":
            self.add_label(ins.ops[1].val, "dbcc")
            return True
        if m == "bsr":
            t = ins.ops[0].val
            self.add_entry(t, "bsr")
            self.calls[ins.addr].add(t)
            if self.inline_text_call(ins, t):
                return False
            self.add_label(ins.next, "ret")
            self.check_lea_ret(ins)
            if self.is_trap_wrapper(t):
                self.trap_args(ins)
            return True
        if m in ("jsr", "jmp"):
            is_call = m == "jsr"
            targets = self.resolve_jump(ins, is_call)
            for t in targets:
                if is_call:
                    self.add_entry(t, "jsr")
                    self.calls[ins.addr].add(t)
                else:
                    self.add_label(t, "jmp")
                    self.jumps[ins.addr].add(t)
            if is_call:
                if len(targets) == 1 and self.inline_text_call(ins, targets[0]):
                    return False
                self.add_label(ins.next, "ret")
                self.check_lea_ret(ins)
                if any(self.is_trap_wrapper(t) for t in targets):
                    self.trap_args(ins)
                return True
            self.check_lea_ret(ins)
            # llamada en cola a un envoltorio de trap (`movea.l #tarea,a0 ; jmp $0b44.w`): a0 también es el PC de una
            # tarea nueva (0x94FD8 → 0x94C9E, la tarea de los finales; hito 6)
            if any(self.is_trap_wrapper(t) for t in targets):
                self.trap_args(ins)
            return False
        if m == "trap":
            vec = 32 + ins.ops[0].val
            h = self.rr.rd32(vec * 4)
            if h is not None:
                self.add_entry(h, "trap")
                self.calls[ins.addr].add(h)
            self.add_label(ins.next, "ret")
            self.trap_args(ins)
            return True
        if m in ("rts", "rte", "rtr"):
            self.check_lea_ret(ins)
            return False
        if m == "illegal":
            return False
        if m == "lea":
            src = ins.ops[0]
            t = src.abs_addr()
            if t is not None and self.is_code_addr(t) and ins.addr < t <= ins.addr + 64:
                self.pending_lea_ret[t] = (ins.addr, ins.ops[1].reg)
            return True
        if m == "pea":
            t = ins.ops[0].abs_addr()
            if t is not None and self.is_code_addr(t) and t >= 0x400:
                self.code_ptrs.add(t)
                self.add_entry(t, "pea")
            return True
        if m == "stop":
            return True
        return True

    def inline_text_call(self, ins: Insn, target: int) -> bool:
        """`bsr rutina` seguida de un texto en línea (hint inline_text): la rutina lee el texto desde la dirección de
        retorno y vuelve detrás de él.  Formato del texto de 0x8A284 (herramientas de desarrollo): fila, columna y
        atributo, caracteres hasta 0; '/' empieza otra posición (fila, columna, atributo); al final se alinea a par."""
        if target not in self.hints.get("inline_text", ()):
            return False
        a = ins.next
        limit = a + 1024
        while a < limit:
            a += 3
            while a < limit:
                ch = self.rom[a]
                a += 1
                if ch == 0:
                    end = a + (a & 1)
                    for p in range(ins.next, end):
                        if self.kind[p] == UNKNOWN:
                            self.kind[p] = TABLE
                    self.inline_ret[ins.addr] = end
                    self.add_label(end, "ret")
                    return True
                if ch == 0x2F:
                    break
        return False

    def trap_args(self, ins: Insn):
        """Argumentos de las llamadas al sistema: punteros a código en a0/a1 (creación de tareas).
        El puntero puede ser una constante o venir de una tabla de punteros indexada."""
        for r in (0, 1):
            v = self.value_of(ins, AN, r)
            if isinstance(v, Const) and self.is_code_addr(v.val) and v.val >= 0x400:
                self.code_ptrs.add(v.val)
                self.add_entry(v.val, f"trap-a{r}")
            elif isinstance(v, TableLong):
                t = self.long_table(v.base, ins.addr, True)
                if t is not None:
                    t.users.add(ins.addr)
                    t.is_call = True
                    for tgt in t.targets:
                        self.code_ptrs.add(tgt)
                        self.add_entry(tgt, "trap-tabla")

    def is_trap_wrapper(self, target: int) -> bool:
        ins = self.dec.decode(target)
        return ins is not None and ins.mnem == "trap"

    def check_lea_ret(self, ins: Insn):
        """Idioma lea ret(pc),An ; bra/jmp sub: 'ret' sigue a la transferencia -> entrada.

        Solo se acepta si (1) la lea y la transferencia están en el mismo tramo lineal (ninguna rama ni salto entre
        ellas) y (2) la subrutina vuelve de verdad por ese registro (alcanza un `jmp (An)` con el mismo An).  Sin estas
        dos condiciones `lea tabla(pc),a0 ; jmp rutina` (paso de un puntero a datos, p. ej. 0xE768 → 0x4854 con la
        tabla de animación en 0xE77E) se tomaba por un retorno y los datos se decodificaban como código, tapando una
        instrucción real (0xE790, hito 6)."""
        t = ins.next
        p = self.pending_lea_ret.get(t)
        if p is None:
            return
        lea_addr, reg = p
        a = lea_addr
        while True:
            li = self.insns.get(a) or self.dec.decode(a)
            if li is None:
                return
            if a == ins.addr:
                break
            if a != lea_addr and li.mnem in ("bcc", "dbcc", "bsr", "jsr", "jmp", "rts", "rte", "rtr", "trap", "illegal"):
                return
            a = li.next
            if a > ins.addr:
                return
        dest = None
        if ins.mnem in ("bcc", "bsr"):
            dest = ins.ops[0].val
        elif ins.mnem in ("jmp", "jsr"):
            dest = ins.ops[0].abs_addr()
        if dest is None or not self.returns_via(dest & 0xFFFFFF, reg):
            return
        self.add_entry(t, "lea-ret")

    def returns_via(self, start: int, reg: int, limit: int = 4000) -> bool:
        """¿la subrutina que empieza en `start` alcanza un `jmp (An)` con An = reg?  Recorrido del flujo sin entrar en
        las llamadas, sobre el decodificador (no sobre el estado del análisis, que puede no conocerla todavía)."""
        seen = set()
        stack = [start]
        while stack and len(seen) < limit:
            a = stack.pop()
            if a in seen or not self.is_code_addr(a):
                continue
            seen.add(a)
            ins = self.dec.decode(a)
            if ins is None:
                continue
            m = ins.mnem
            if m == "jmp":
                op = ins.ops[0]
                if op.kind == IND and op.reg == reg:
                    return True
                t = op.abs_addr()
                if t is not None:
                    stack.append(t & 0xFFFFFF)
                continue
            if m in ("rts", "rte", "rtr", "illegal"):
                continue
            if m == "bcc":
                stack.append(ins.ops[0].val)
                if ins.cc != 0:
                    stack.append(ins.next)
                continue
            if m == "dbcc":
                stack.append(ins.ops[1].val)
            stack.append(ins.next)
        return False

    # ---- evaluador simbólico hacia atrás -----------------------------------------------
    def writes(self, ins: Insn) -> Set[Tuple[str, int]]:
        m = ins.mnem
        w = set()
        if m in ("movem",) and ins.ops[1].kind == "reglist":
            mask = ins.ops[1].val
            for i in range(16):
                if mask & (1 << i):
                    w.add((DN if i < 8 else AN, i & 7))
        if m in ("jsr", "bsr", "trap"):
            # tras una llamada no sabemos nada de los registros
            return {(DN, i) for i in range(8)} | {(AN, i) for i in range(8)}
        if m == "exg":
            for o in ins.ops:
                w.add((o.kind, o.reg))
            return w
        if m in ("link",):
            w.add((AN, ins.ops[0].reg)); w.add((AN, 7))
        if m == "unlk":
            w.add((AN, ins.ops[0].reg)); w.add((AN, 7))
        if m in ("divu", "divs", "mulu", "muls", "chk"):
            w.add((DN, ins.ops[1].reg))
        for o in ins.ops:
            if o.kind in (POSTINC, PREDEC):
                w.add((AN, o.reg))
        if not ins.ops:
            return w
        dst = ins.ops[-1]
        if m in ("tst", "cmp", "cmpa", "cmpi", "cmpm", "btst", "pea", "jmp", "jsr", "bcc", "bsr", "dbcc",
                 "movep" if dst.kind == D16AN else "", "chk"):
            if m == "movep":
                return w
            if m == "dbcc":
                w.add((DN, ins.ops[0].reg))
            return w
        if dst.kind in (DN, AN):
            w.add((dst.kind, dst.reg))
        if m == "move" and dst.kind == "usp":
            return w
        return w

    def value_of(self, start: Insn, kind: str, n: int, depth=0):
        """Valor simbólico del registro (kind, n) justo antes de ejecutar `start`."""
        if depth > 6:
            return UNKNOWN_VAL
        cur = self.prev.get(start.addr)
        for _ in range(16):
            if cur is None:
                return UNKNOWN_VAL
            ins = self.insns[cur]
            if (kind, n) in self.writes(ins):
                return self.interpret(ins, kind, n, depth)
            cur = self.prev.get(cur)
        return UNKNOWN_VAL

    def interpret(self, ins: Insn, kind: str, n: int, depth: int):
        m = ins.mnem
        if m in ("jsr", "bsr", "trap", "movem", "exg", "link", "unlk", "divu", "divs", "mulu", "muls"):
            return UNKNOWN_VAL
        if not ins.ops:
            return UNKNOWN_VAL
        dst = ins.ops[-1]
        if dst.kind != kind or dst.reg != n:
            # escrito como efecto lateral (postinc/predec) -> desconocido
            return UNKNOWN_VAL
        if m == "lea":
            return self.ea_address(ins.ops[0], ins, depth)
        if m in ("movea", "move") and ins.size == 4 and kind == AN:
            src = ins.ops[0]
            if src.kind == IMM:
                return Const(src.val & 0xFFFFFF)
            if src.kind in (DN, AN):
                v = self.value_of(ins, src.kind, src.reg, depth + 1)
                return v if isinstance(v, Const) else UNKNOWN_VAL
            if src.kind in (ABSW, ABSL, D16PC):
                a = src.abs_addr()
                if self.in_rom(a):
                    v = self.rr.rd32(a)
                    return Const(v & 0xFFFFFF) if v is not None else UNKNOWN_VAL
                return UNKNOWN_VAL
            if src.kind == D8PCXN:
                return TableLong((src.pcbase + src.disp) & 0xFFFFFF)
            if src.kind == D8ANXN:
                b = self.value_of(ins, AN, src.reg, depth + 1)
                if isinstance(b, Const):
                    return TableLong((b.val + src.disp) & 0xFFFFFF)
                return UNKNOWN_VAL
            if src.kind in (IND, POSTINC):
                b = self.value_of(ins, AN, src.reg, depth + 1)
                if isinstance(b, Const):
                    return TableLong(b.val)
                return UNKNOWN_VAL
            if src.kind == D16AN:
                b = self.value_of(ins, AN, src.reg, depth + 1)
                if isinstance(b, Const) and self.in_rom(b.val + src.disp):
                    v = self.rr.rd32((b.val + src.disp) & 0xFFFFFF)
                    return Const(v & 0xFFFFFF) if v is not None else UNKNOWN_VAL
                return UNKNOWN_VAL
            return UNKNOWN_VAL
        if m == "move" and kind == DN:
            src = ins.ops[0]
            if ins.size == 2:
                if src.kind == D8PCXN:
                    return TableWord((src.pcbase + src.disp) & 0xFFFFFF)
                if src.kind == D8ANXN:
                    b = self.value_of(ins, AN, src.reg, depth + 1)
                    if isinstance(b, Const):
                        return TableWord((b.val + src.disp) & 0xFFFFFF)
                    return Index()
                if src.kind == IMM:
                    return Const(sext16(src.val) & 0xFFFFFFFF)
            if src.kind == IMM and ins.size == 4:
                return Const(src.val)
            return Index()
        if m == "moveq":
            return Const(ins.ops[0].val)
        if kind == DN:
            return Index()
        return UNKNOWN_VAL

    def ea_address(self, op: Op, ins: Insn, depth: int):
        a = op.abs_addr()
        if a is not None:
            return Const(a & 0xFFFFFF)
        if op.kind == D16AN:
            b = self.value_of(ins, AN, op.reg, depth + 1)
            if isinstance(b, Const):
                return Const((b.val + op.disp) & 0xFFFFFF)
        if op.kind == IND:
            b = self.value_of(ins, AN, op.reg, depth + 1)
            if isinstance(b, Const):
                return b
        return UNKNOWN_VAL

    # ---- resolución de jmp/jsr ----------------------------------------------------------
    def resolve_jump(self, ins: Insn, is_call: bool) -> List[int]:
        ea = ins.ops[0]
        a = ea.abs_addr()
        if a is not None:
            return [a & 0xFFFFFF]
        info = ""
        targets: List[int] = []
        table: Optional[Table] = None
        if ea.kind in (D8PCXN, D8ANXN):
            if ea.kind == D8PCXN:
                base = (ea.pcbase + ea.disp) & 0xFFFFFF
            else:
                b = self.value_of(ins, AN, ea.reg)
                if not isinstance(b, Const):
                    info = f"a{ea.reg}={b}"
                    base = None
                else:
                    base = (b.val + ea.disp) & 0xFFFFFF
            if base is not None:
                xv = self.value_of(ins, AN if ea.ixan else DN, ea.ixreg)
                if isinstance(xv, TableWord):
                    table = self.word_table(xv.base, base, ins.addr, is_call)
                    info = f"tabla.w en {xv.base:#x} rel {base:#x}"
                elif isinstance(xv, Const):
                    targets = [(base + sext16(xv.val)) & 0xFFFFFF] if not ea.ixlong else [(base + xv.val) & 0xFFFFFF]
                    info = f"indice constante {xv.val:#x}"
                else:
                    table = self.code_table(base, ins.addr, is_call)
                    info = f"tabla de codigo en {base:#x} (indice {xv})"
                    if table is None:
                        info = f"indice {xv} sin tabla de codigo reconocible en {base:#x}"
        elif ea.kind in (IND, D16AN):
            v = self.value_of(ins, AN, ea.reg)
            disp = ea.disp if ea.kind == D16AN else 0
            if isinstance(v, Const):
                targets = [(v.val + disp) & 0xFFFFFF]
                info = f"a{ea.reg}={v.val:#x}"
            elif isinstance(v, TableLong) and disp == 0:
                table = self.long_table(v.base, ins.addr, is_call)
                info = f"tabla.l en {v.base:#x}"
            else:
                info = f"a{ea.reg}={v}" + (" (retorno por registro)" if v is None and ea.kind == IND else "")
        if table is not None:
            targets = list(table.targets)
            table.users.add(ins.addr)
            table.is_call = table.is_call or is_call
        resolved = bool(targets)
        self.indirects[ins.addr] = Indirect(ins.addr, ins.text(), info, is_call, resolved, targets,
                                            table.base if table else None)
        return [t for t in targets if self.is_code_addr(t)]

    def table_limit(self, base: int, start: int) -> int:
        lim = ROM_END
        if base in self.table_end_override:
            return self.table_end_override[base]
        for h in self.hard_limits:
            if start < h < lim:
                lim = h
        return lim

    def mark_table(self, t: Table):
        for i in range(t.base, t.end):
            if self.kind[i] == UNKNOWN:
                self.kind[i] = TABLE
        self.tables[t.base] = t

    def word_table(self, base: int, rel: int, user: int, is_call: bool) -> Optional[Table]:
        if base in self.tables and self.tables[base].kind == "word":
            return self.tables[base]
        t = Table(base, "word", rel)
        p = base
        # la base puede caer dentro del propio idioma (los índices empiezan en 2, 4...): saltar el código
        while p < ROM_END and self.kind[p] == CODE and p < base + 16:
            p += 2
        t.base = p
        limit = self.table_limit(base, p)
        while p + 2 <= limit and len(t.targets) < 1024:
            if self.kind[p] == CODE or (p in self.labels and p != t.base):
                break
            w = self.rr.rd16(p)
            if w is None:
                break
            tgt = (rel + sext16(w)) & 0xFFFFFF
            if not self.is_code_addr(tgt) or base <= tgt <= p + 1:
                break
            t.targets.append(tgt)
            p += 2
            if tgt > base and tgt < limit:
                limit = tgt
        t.end = p
        if not t.targets:
            return None
        self.mark_table(t)
        self.tables[base] = t
        return t

    def long_table(self, base: int, user: int, is_call: bool) -> Optional[Table]:
        if base in self.tables and self.tables[base].kind == "long":
            return self.tables[base]
        t = Table(base, "long", base)
        p = base
        while p < ROM_END and self.kind[p] == CODE and p < base + 16:
            p += 4
        t.base = p
        limit = self.table_limit(base, p)
        while p + 4 <= limit and len(t.targets) < 1024:
            if self.kind[p] == CODE or (p in self.labels and p != t.base):
                break
            v = self.rr.rd32(p)
            if v is None:
                break
            tgt = v & 0xFFFFFF
            if (v >> 24) or not self.is_code_addr(tgt) or tgt < 0x400 or base <= tgt <= p + 3:
                break
            t.targets.append(tgt)
            p += 4
            if tgt > base and tgt < limit:
                limit = tgt
        t.end = p
        if not t.targets:
            return None
        self.mark_table(t)
        self.tables[base] = t
        return t

    def code_table(self, base: int, user: int, is_call: bool) -> Optional[Table]:
        """Secuencia de bra/jmp de paso fijo a partir de base (jmp base(pc,d0.w) con d0 = n*paso)."""
        if base in self.tables and self.tables[base].kind == "code":
            return self.tables[base]
        ins0 = self.dec.decode(base)
        if ins0 is None or ins0.mnem not in ("bcc", "jmp", "bsr", "jsr") or (ins0.mnem == "bcc" and ins0.cc != 0):
            return None
        stride = ins0.length
        t = Table(base, "code", base)
        p = base
        limit = self.table_limit(base, base)
        while p + stride <= limit and len(t.targets) < 512:
            if p != base and (p in self.labels and self.label_src.get(p) not in ("code-table",)):
                break
            ins = self.dec.decode(p)
            if ins is None or ins.length != stride or ins.mnem != ins0.mnem or (ins.mnem == "bcc" and ins.cc != 0):
                break
            if ins.mnem == "jmp" and ins.ops[0].abs_addr() is None:
                break
            t.targets.append(p)
            if p != base:
                self.add_label(p, "code-table")
            p += stride
        t.end = p
        if len(t.targets) < 2:
            return None
        self.tables[base] = t     # las entradas son código, no se marcan como TABLE
        return t

    # ---- bucle principal ---------------------------------------------------------------
    def run(self):
        self.reset()
        # Tabla de vectores: 0x000-0x3FF
        for i in range(0, 0x400):
            self.kind[i] = VECTOR
        self.add_entry(self.rr.rd32(4), "reset")
        for v in range(2, 64):
            h = self.rr.rd32(v * 4)
            if h is not None and self.is_code_addr(h) and h >= 0x400:
                self.add_entry(h, f"vector{v}")
        for a in self.hints.get("entries", []):
            self.add_entry(a, "hint")
        for a in self.hints.get("labels", []):
            self.add_label(a, "hint")
        while self.worklist:
            a, src = self.worklist.pop()
            if a in self.insns:
                continue
            self.sweep(a)
        return self

    def iterate(self, rounds=3):
        """Repite el análisis acotando las tablas con lo aprendido (bases de tabla, destinos directos)."""
        self.hard_limits = set()
        for r in range(rounds):
            self.run()
            new_limits = set(self.tables.keys())
            # cualquier motivo de confianza basta: una dirección a la que salta una rama directa no puede estar dentro
            # de una tabla aunque una tabla la nombrara antes (0x36984, hito 6: la tabla de 0x3697E se comía ese código)
            trusted = ("bcc", "dbcc", "bsr", "jsr", "jmp", "ret", "reset", "trap", "lea-ret", "hint")
            for a, srcs in self.label_srcs.items():
                if any(src in trusted or src.startswith("vector") for src in srcs):
                    new_limits.add(a)
            # las entradas de jmp/jsr directos también
            for ins in self.insns.values():
                if ins.mnem in ("jmp", "jsr") and ins.ops[0].abs_addr() is not None:
                    new_limits.add(ins.ops[0].abs_addr() & 0xFFFFFF)
            if new_limits == self.hard_limits:
                break
            self.hard_limits = new_limits
        return self

    # ---- particion en funciones --------------------------------------------------------
    def edges(self, ins: Insn) -> List[int]:
        """Sucesores intra-procedimentales (sin destinos de llamada)."""
        m = ins.mnem
        out = []
        if m == "bcc":
            out.append(ins.ops[0].val)
            if ins.cc != 0:
                out.append(ins.next)
        elif m == "dbcc":
            out += [ins.ops[1].val, ins.next]
        elif m == "jmp":
            out += sorted(self.jumps.get(ins.addr, ()))
        elif m in ("rts", "rte", "rtr", "illegal"):
            pass
        else:
            out.append(self.inline_ret.get(ins.addr, ins.next))
        return [t for t in out if t in self.insns]

    def partition(self):
        """Para cada entrada, el conjunto de instrucciones alcanzables sin cruzar otras entradas."""
        funcs: Dict[int, Set[int]] = {}
        for e in sorted(self.entries):
            if e not in self.insns:
                continue
            body = set()
            stack = [e]
            while stack:
                a = stack.pop()
                if a in body:
                    continue
                if a != e and a in self.entries:
                    continue
                body.add(a)
                for t in self.edges(self.insns[a]):
                    if t not in body:
                        stack.append(t)
            funcs[e] = body
        self.funcs = funcs
        # propietario de cada etiqueta: la función con entrada más cercana por debajo que la contenga
        owner: Dict[int, int] = {}
        for e, body in funcs.items():
            for a in body:
                if a in self.labels:
                    cur = owner.get(a)
                    if cur is None or (e <= a and (cur > a or e > cur)):
                        owner[a] = e
        self.owner = owner
        return funcs

    # ---- informes ----------------------------------------------------------------------
    def coverage(self) -> dict:
        counts = defaultdict(int)
        for k in self.kind:
            counts[k] += 1
        total = len(self.rom)
        res = {KIND_NAMES[k]: (counts[k], 100.0 * counts[k] / total) for k in (CODE, TABLE, VECTOR, UNKNOWN)}
        return res

    def unknown_regions(self, min_size=64) -> List[Tuple[int, int]]:
        regs = []
        a = 0
        n = len(self.kind)
        while a < n:
            if self.kind[a] == UNKNOWN:
                b = a
                while b < n and self.kind[b] == UNKNOWN:
                    b += 1
                if b - a >= min_size:
                    regs.append((a, b))
                a = b
            else:
                a += 1
        return regs

    def undecoded_successors(self) -> Dict[int, List[int]]:
        """destino (rama, llamada directa o caída a la siguiente) que no es el comienzo de una instrucción decodificada
        -> instrucciones que llevan a él.  Cada uno es un conflicto del análisis (datos tomados por código o una tabla
        demasiado larga) que el generador convierte en un error en tiempo de ejecución; debe ser 0 (hito 6)."""
        need: Dict[int, List[int]] = {}
        for a, ins in self.insns.items():
            m = ins.mnem
            succ = []
            if m in ("bcc", "bsr"):
                succ.append(ins.ops[0].val)
            if m == "dbcc":
                succ.append(ins.ops[1].val)
            if m in ("jmp", "jsr") and ins.ops[0].abs_addr() is not None:
                succ.append(ins.ops[0].abs_addr() & 0xFFFFFF)
            if not (m in ("rts", "rte", "rtr", "illegal", "jmp") or (m == "bcc" and ins.cc == 0)):
                succ.append(self.inline_ret.get(a, ins.next))
            for t in succ:
                if t < ROM_END and t not in self.insns:
                    need.setdefault(t, []).append(a)
        return need

    def report(self) -> str:
        out = []
        cov = self.coverage()
        out.append("== Cobertura del ROM (0x%X bytes) ==" % len(self.rom))
        for name, (n, pct) in cov.items():
            out.append(f"  {name:<12} {n:8d} bytes  {pct:6.2f}%")
        out.append(f"  instrucciones: {len(self.insns)}   entradas: {len(self.entries)}   etiquetas: {len(self.labels)}")
        if hasattr(self, "funcs"):
            dup = sum(len(b) for b in self.funcs.values()) - len(set().union(*self.funcs.values())) if self.funcs else 0
            out.append(f"  funciones: {len(self.funcs)}   instrucciones duplicadas entre funciones: {dup}")
        ntab = defaultdict(int)
        for t in self.tables.values():
            ntab[t.kind] += 1
        out.append(f"  tablas de salto: {dict(ntab)}  (entradas totales {sum(len(t.targets) for t in self.tables.values())})")
        unres = [i for i in self.indirects.values() if not i.resolved]
        res = [i for i in self.indirects.values() if i.resolved]
        out.append(f"  jmp/jsr indirectos: {len(self.indirects)} ({len(res)} resueltos, {len(unres)} sin resolver)")
        out.append(f"  fallos de decodificacion (fin de barrido): {len(self.bad_decode)}   barridos solapados: {len(self.overlaps)}")
        und = self.undecoded_successors()
        out.append(f"  destinos sin decodificar (conflictos; deben ser 0): {len(und)}"
                   + ("  " + " ".join("%06x<-%06x" % (t, srcs[0]) for t, srcs in sorted(und.items())[:12]) if und else ""))
        out.append(f"  llamadas a rutinas de texto en linea (hint inline_text): {len(self.inline_ret)}")
        out.append("")
        out.append("== Saltos indirectos sin resolver ==")
        for i in sorted(unres, key=lambda x: x.addr):
            out.append(f"  {i.addr:06x}: {i.text:<28} {i.info}")
        out.append("")
        out.append("== Tablas de salto ==")
        for b in sorted(self.tables):
            t = self.tables[b]
            users = ",".join(f"{u:06x}" for u in sorted(t.users))
            out.append(f"  {b:06x} {t.kind:<4} {len(t.targets):4d} entradas  fin {t.end:06x}  {'jsr' if t.is_call else 'jmp'} desde {users}")
        out.append("")
        out.append("== Regiones desconocidas (>= 256 bytes) ==")
        for a, b in self.unknown_regions(256):
            out.append(f"  {a:06x}-{b:06x} ({b - a} bytes)")
        if self.bad_decode:
            out.append("")
            out.append("== Direcciones donde el decodificador se detuvo (posible dato o instrucción no 68000) ==")
            for a, w in sorted(self.bad_decode)[:50]:
                out.append(f"  {a:06x}: {w:04x}")
        return "\n".join(out)

    def disasm(self) -> str:
        lines = []
        for a in sorted(self.insns):
            ins = self.insns[a]
            tag = ""
            if a in self.entries:
                tag = f"\nF_{a:06X}:  ; entrada ({self.entry_src.get(a)})"
            elif a in self.labels:
                tag = f"L_{a:06X}:"
            if tag:
                lines.append(tag)
            ws = " ".join(f"{w:04x}" for w in ins.words)
            extra = ""
            if a in self.indirects:
                extra = "   ; " + self.indirects[a].info
            lines.append(f"  {a:06x}: {ws:<26} {ins.text()}{extra}")
        return "\n".join(lines)


def load_hints(path: str) -> dict:
    hints = {"entries": [], "labels": [], "table_end": {}}
    if not os.path.exists(path):
        return hints
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if parts[0] == "entry":
                hints["entries"].append(int(parts[1], 16))
            elif parts[0] == "label":
                hints["labels"].append(int(parts[1], 16))
            elif parts[0] == "table_end":
                hints["table_end"][int(parts[1], 16)] = int(parts[2], 16)
            elif parts[0] == "inline_text":
                hints.setdefault("inline_text", []).append(int(parts[1], 16))
    return hints


def analyze(rom_path: str, hints_path: Optional[str] = None) -> Analysis:
    rom = open(rom_path, "rb").read()
    hints = load_hints(hints_path) if hints_path else {}
    an = Analysis(rom, hints)
    an.iterate()
    an.partition()
    return an


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--hints", default=os.path.join(os.path.dirname(__file__), "hints.txt"))
    ap.add_argument("--report", help="archivo de informe de cobertura")
    ap.add_argument("--disasm", help="archivo de desensamblado completo")
    ap.add_argument("--insns", help="lista 'addr len texto' para dasmcheck")
    args = ap.parse_args()
    an = analyze(args.rom, args.hints)
    rep = an.report()
    print(rep)
    if args.report:
        with open(args.report, "w") as f:
            f.write(rep + "\n")
    if args.disasm:
        with open(args.disasm, "w") as f:
            f.write(an.disasm() + "\n")
    if args.insns:
        with open(args.insns, "w") as f:
            for a in sorted(an.insns):
                ins = an.insns[a]
                f.write(f"{a:06x} {ins.length} {ins.text()}\n")


if __name__ == "__main__":
    main()
