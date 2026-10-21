#!/usr/bin/env python3
"""mame_m68000_ack.py - punto en que el 68000 de MAME 0.289 decide si atiende una interrupción, por instrucción (hito 6, D47).

El núcleo m68000 de MAME (src/devices/cpu/m68000/m68000-sdf.cpp, generado de su microcódigo) solo devuelve el control al
planificador al terminar un acceso al bus (`if(m_icount <= 0) ... return;` tras cada lectura o escritura), y decide si la
instrucción siguiente es una interrupción en la microinstrucción que pasa IR a IRD (`m_next_state = m_int_next_state`).  Una
IRQ afirmada en el ciclo V solo la ve ese «latch» si antes de él ha terminado un acceso en un ciclo >= V (ahí se suspende el
núcleo, el planificador dispara el VBlank y afirma la línea).  Así que la IRQ se atiende tras la instrucción I si
V <= A(I), con A(I) = fin del último acceso al bus anterior al último latch de I (que puede ser de una instrucción anterior).

Este programa lee el código generado de MAME, recorre cada manejador (ramas `if(m_t)` y bucles acotados) y escribe, por
patrón de opcode (valor/máscara del comentario del manejador) y camino, tres números relativos al comienzo de la
instrucción: ciclos totales, K = fin del último acceso antes del último latch (vacío si no hay ninguno: entonces vale el de
una instrucción anterior) y E = fin del último acceso de la instrucción (vacío si no hay).  La tabla
(recompiler/m68000_ack.txt) la usa gen_c.py; no contiene código de MAME, solo esos números.

  python3 tools/mame_m68000_ack.py external/mame/src/devices/cpu/m68000/m68000-sdf.cpp recompiler/m68000_ack.txt
"""
import re
import sys

FUNC = re.compile(r"^void m68000_device::(\w+)_df\(\) // ([0-9a-f]{4}) ([0-9a-f]{4})")


def parse_handlers(path):
    """{nombre: (valor, máscara, [líneas])} de los manejadores de instrucción completos (_df)"""
    out = {}
    cur = None
    body = []
    depth = 0
    for line in open(path, encoding="utf-8", errors="replace"):
        if cur is None:
            m = FUNC.match(line)
            if m:
                cur = (m.group(1), int(m.group(2), 16), int(m.group(3), 16))
                body = []
                depth = 0
            continue
        depth += line.count("{") - line.count("}")
        if depth == 0:                        # llave de cierre de la función
            out[cur[0]] = (cur[1], cur[2], body)
            cur = None
            continue
        if not (depth == 1 and line.strip() == "{"):
            body.append(line.rstrip("\n"))
    return out


def skip_block(lines, i):
    """salta una sentencia if con bloque entre llaves (o de una sola línea) que empieza en lines[i]"""
    if "{" not in lines[i]:
        return i + 2
    depth = 0
    while i < len(lines):
        depth += lines[i].count("{") - lines[i].count("}")
        i += 1
        if depth == 0:
            return i
    return i


def compile_body(lines):
    """operaciones: ('label', n), ('t', ciclos), ('acc',), ('latch',), ('goto', n), ('br', [n...]), ('end',)"""
    ops = []
    i = 0
    n = len(lines)
    while i < n:
        s = lines[i].strip()
        if not s or s.startswith("//"):
            i += 1
            continue
        if re.match(r"^\w+:$", s):
            ops.append(("label", s[:-1]))
            i += 1
            continue
        if s.startswith("if(m_icount <= 0 && access_to_be_redone())"):
            # lectura de TAS (lectura-modificación-escritura): no suspende, así que no es un punto donde se vea la IRQ
            for k in range(len(ops) - 1, -1, -1):
                if ops[k][0] == "acc":
                    ops[k] = ("acc_ns",)
                    break
            i = skip_block(lines, i)
            continue
        if (s.startswith("if(m_icount <= 0)") or s.startswith("if(m_aob & 1)") or s.startswith("if(!m_tas")
                or s.startswith("if(!(m_sr & SR_S))")):
            i = skip_block(lines, i)          # suspensión, error de dirección, privilegio, gancho de TAS: fuera del camino
            continue
        m = re.match(r"^m_icount -= (\d+);", s)
        if m:
            ops.append(("t", int(m.group(1))))
            i += 1
            continue
        if "_interruptible(" in s:
            ops.append(("acc",))
            i += 1
            continue
        if "m_next_state = m_int_next_state" in s:
            ops.append(("latch",))
            i += 1
            continue
        m = re.match(r"^goto (\w+);", s)
        if m:
            ops.append(("goto", m.group(1)))
            i += 1
            continue
        if s.startswith("if(m_t"):
            targets = []
            i += 1
            last_else = False
            while i < n:
                s2 = lines[i].strip()
                if s2.startswith("goto "):
                    targets.append(re.match(r"goto (\w+);", s2).group(1))
                    i += 1
                    if last_else:
                        break
                elif s2 == "else":
                    last_else = True
                    i += 1
                elif s2.startswith("else if"):
                    i += 1
                else:
                    break
            ops.append(("br", targets))
            continue
        if s == "return;":
            ops.append(("end",))
            i += 1
            continue
        i += 1
    return ops


def paths(ops, limit=24):
    """todos los caminos (cada etiqueta como mucho `limit` veces): {(total, K, E)}; tras ('acc',) el siguiente ('t', n) es
    la duración del acceso"""
    labels = {op[1]: k for k, op in enumerate(ops) if op[0] == "label"}
    results = set()
    stack = [(0, 0, None, None, None, False, {})]
    steps = 0
    while stack and steps < 400000:
        steps += 1
        pc, t, last, K, E, pend, visits = stack.pop()
        while pc < len(ops):
            op = ops[pc]
            kind = op[0]
            if kind == "label":
                v = visits.get(op[1], 0) + 1
                if v > limit:
                    pc = None
                    break
                visits = dict(visits)
                visits[op[1]] = v
                pc += 1
            elif kind == "t":
                t += op[1]
                if pend:
                    last = E = t
                    pend = False
                pc += 1
            elif kind == "acc":
                pend = True
                pc += 1
            elif kind == "acc_ns":
                pend = False
                pc += 1
            elif kind == "latch":
                K = last
                pc += 1
            elif kind == "goto":
                pc = labels[op[1]]
            elif kind == "br":
                for tg in op[1][1:]:
                    stack.append((labels[tg], t, last, K, E, pend, visits))
                pc = labels[op[1][0]]
            elif kind == "end":
                results.add((t, K, E))
                pc = None
                break
        # caminos que agotan las visitas de una etiqueta (bucles largos) se descartan
    return sorted(results, key=lambda r: (r[0], -1 if r[1] is None else r[1], -1 if r[2] is None else r[2]))


def decode_order(decode_cpp, sdf):
    """[(valor, máscara, nombre)] en el orden de s_packed_decode_table (gana la primera coincidencia)"""
    names = []
    inh = False
    for line in open(sdf, encoding="utf-8", errors="replace"):
        if "s_handlers_df[]" in line:
            inh = True
            continue
        if inh:
            m = re.match(r"\s*&m68000_device::(\w+)_df,", line)
            if m:
                names.append(m.group(1))
            elif "};" in line:
                break
    out = []
    for line in open(decode_cpp):
        m = re.match(r"\s*\{ 0x([0-9a-f]+), 0x([0-9a-f]+),\s*(\d+) \}", line)
        if m:
            out.append((int(m.group(1), 16), int(m.group(2), 16), names[int(m.group(3))]))
    return out


def main():
    src, out = sys.argv[1], sys.argv[2]
    decode_cpp = sys.argv[3] if len(sys.argv) > 3 else src.replace("m68000-sdf.cpp", "m68000-decode.cpp")
    hs = parse_handlers(src)
    order = decode_order(decode_cpp, src)
    with open(out, "w") as f:
        f.write("# m68000_ack.txt - generado por tools/mame_m68000_ack.py desde el microcódigo del 68000 de MAME 0.289 (D47).\n")
        f.write("# Una línea por entrada de la tabla de decodificación de MAME, en su orden (vale la primera con\n")
        f.write("# opcode & máscara == valor): valor máscara manejador | caminos total/K/E, con K = fin del último acceso al\n")
        f.write("# bus anterior al último latch de interrupción y E = fin del último acceso, en ciclos desde el comienzo de la\n")
        f.write("# instrucción ('-' = ninguno en esta instrucción).  Solo números: no contiene código de MAME.\n")
        for val, mask, name in order:
            if name not in hs:
                continue
            ps = paths(compile_body(hs[name][2]))
            f.write("%04x %04x %s | %s\n" % (val, mask, name, " ".join(
                "%d/%s/%s" % (t, "-" if K is None else K, "-" if E is None else E) for t, K, E in ps)))
    print("%d entradas de decodificación, %d manejadores, en %s" % (len(order), len(hs), out))


if __name__ == "__main__":
    main()
