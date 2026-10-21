/*
 * z80.c - intérprete Z80 (ver z80.h).
 *
 * Temporización: cada instrucción es una secuencia de ciclos M; `t` cuenta los ciclos T transcurridos desde
 * el comienzo de la instrucción y cada acceso al bus se hace con el ciclo absoluto z->cycles + t del
 * comienzo de su ciclo M (M1 = 4, lectura/escritura de memoria = 3, E/S = 4, más los ciclos internos).
 * La división en ciclos M es la del Z80 de Zilog; la descripción del núcleo de MAME (z80.lst) se usó solo
 * para confirmar dónde caen los ciclos internos en las instrucciones con desplazamiento (IX+d).
 */
#include "z80.h"
#include "state.h"
#include <stdio.h>
#include <string.h>

#define CF 0x01
#define NF 0x02
#define PF 0x04
#define XF 0x08
#define HF 0x10
#define YF 0x20
#define ZF 0x40
#define SF 0x80

static uint8_t sz53[256], sz53p[256];
static int tables_ready;

static void init_tables(void) {
    for (int v = 0; v < 256; v++) {
        int p = v;
        p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
        sz53[v] = (uint8_t)((v & (SF | YF | XF)) | (v == 0 ? ZF : 0));
        sz53p[v] = (uint8_t)(sz53[v] | ((p & 1) ? 0 : PF));
    }
    tables_ready = 1;
}

#define A  (z->af.b.h)
#define F  (z->af.b.l)
#define B  (z->bc.b.h)
#define C  (z->bc.b.l)
#define D  (z->de.b.h)
#define E  (z->de.b.l)
#define H  (z->hl.b.h)
#define L  (z->hl.b.l)
#define BC (z->bc.w)
#define DE (z->de.w)
#define HL (z->hl.w)
#define SP (z->sp)
#define PC (z->pc)
#define WZ (z->wz.w)

/* ---- acceso al bus con su ciclo ---------------------------------------------------------- */
static inline uint8_t rd_(Z80 *z, int *t, uint16_t a) {
    uint8_t v = z->bus.read(z->bus.ctx, a, z->cycles + (uint64_t)*t);
    *t += 3;
    return v;
}
static inline void wr_(Z80 *z, int *t, uint16_t a, uint8_t v) {
    z->bus.write(z->bus.ctx, a, v, z->cycles + (uint64_t)*t);
    *t += 3;
}
static inline uint8_t m1_(Z80 *z, int *t) {
    uint8_t v = z->bus.fetch(z->bus.ctx, z->pc, z->cycles + (uint64_t)*t);
    z->pc++;
    z->r++;
    *t += 4;
    return v;
}
static inline uint8_t in_(Z80 *z, int *t, uint16_t port) {
    uint8_t v = z->bus.in ? z->bus.in(z->bus.ctx, port, z->cycles + (uint64_t)*t) : 0xff;
    *t += 4;
    return v;
}
static inline void out_(Z80 *z, int *t, uint16_t port, uint8_t v) {
    if (z->bus.out) z->bus.out(z->bus.ctx, port, v, z->cycles + (uint64_t)*t);
    *t += 4;
}

#define RD(a)     rd_(z, &t, (uint16_t)(a))
#define WR(a, v)  wr_(z, &t, (uint16_t)(a), (uint8_t)(v))
#define M1()      m1_(z, &t)
#define ARG()     rd_(z, &t, z->pc++)
#define IDLE(n)   (t += (n))
#define IN(p)     in_(z, &t, (uint16_t)(p))
#define OUT(p, v) out_(z, &t, (uint16_t)(p), (uint8_t)(v))

static inline uint16_t arg16_(Z80 *z, int *t) {
    uint16_t lo = rd_(z, t, z->pc++);
    uint16_t hi = rd_(z, t, z->pc++);
    return (uint16_t)(lo | (hi << 8));
}
#define ARG16() arg16_(z, &t)

static inline void push16_(Z80 *z, int *t, uint16_t v) {
    z->sp--; wr_(z, t, z->sp, (uint8_t)(v >> 8));
    z->sp--; wr_(z, t, z->sp, (uint8_t)v);
}
static inline uint16_t pop16_(Z80 *z, int *t) {
    uint16_t lo = rd_(z, t, z->sp++);
    uint16_t hi = rd_(z, t, z->sp++);
    return (uint16_t)(lo | (hi << 8));
}
#define PUSH16(v) push16_(z, &t, (uint16_t)(v))
#define POP16()   pop16_(z, &t)

#define SETF(v) do { F = (uint8_t)(v); z->qset = 1; } while (0)

/* ---- ALU ------------------------------------------------------------------------------- */
static inline void alu_add(Z80 *z, uint8_t v, int carry) {
    unsigned a = A, r = a + v + (unsigned)carry;
    SETF(sz53[r & 0xff] | ((a ^ v ^ r) & HF) | ((((a ^ ~v) & (a ^ r)) & 0x80) >> 5) | ((r >> 8) & CF));
    A = (uint8_t)r;
}
static inline void alu_sub(Z80 *z, uint8_t v, int carry) {
    unsigned a = A, r = a - v - (unsigned)carry;
    SETF(sz53[r & 0xff] | ((a ^ v ^ r) & HF) | ((((a ^ v) & (a ^ r)) & 0x80) >> 5) | NF | ((r >> 8) & CF));
    A = (uint8_t)r;
}
static inline void alu_cp(Z80 *z, uint8_t v) {
    unsigned a = A, r = a - v;
    SETF((sz53[r & 0xff] & (SF | ZF)) | (v & (YF | XF)) | ((a ^ v ^ r) & HF) | ((((a ^ v) & (a ^ r)) & 0x80) >> 5) |
         NF | ((r >> 8) & CF));
}
static inline void alu_op(Z80 *z, int op, uint8_t v) {
    switch (op) {
    case 0: alu_add(z, v, 0); break;
    case 1: alu_add(z, v, F & CF); break;
    case 2: alu_sub(z, v, 0); break;
    case 3: alu_sub(z, v, F & CF); break;
    case 4: A &= v; SETF(sz53p[A] | HF); break;
    case 5: A ^= v; SETF(sz53p[A]); break;
    case 6: A |= v; SETF(sz53p[A]); break;
    default: alu_cp(z, v); break;
    }
}
static inline uint8_t alu_inc(Z80 *z, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    SETF((F & CF) | sz53[r] | (r == 0x80 ? PF : 0) | ((r & 0x0f) == 0 ? HF : 0));
    return r;
}
static inline uint8_t alu_dec(Z80 *z, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    SETF((F & CF) | sz53[r] | (r == 0x7f ? PF : 0) | ((r & 0x0f) == 0x0f ? HF : 0) | NF);
    return r;
}
static inline uint16_t alu_add16(Z80 *z, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a + b;
    WZ = (uint16_t)(a + 1);
    SETF((F & (SF | ZF | PF)) | ((r >> 8) & (YF | XF)) | (((a ^ b ^ r) >> 8) & HF) | ((r >> 16) & CF));
    return (uint16_t)r;
}
static inline void alu_adc16(Z80 *z, uint16_t v) {
    uint32_t hl = HL, r = hl + v + (F & CF);
    WZ = (uint16_t)(hl + 1);
    SETF(((r >> 8) & (SF | YF | XF)) | ((r & 0xffff) ? 0 : ZF) | (((hl ^ v ^ r) >> 8) & HF) |
         ((((hl ^ ~(uint32_t)v) & (hl ^ r)) & 0x8000) >> 13) | ((r >> 16) & CF));
    HL = (uint16_t)r;
}
static inline void alu_sbc16(Z80 *z, uint16_t v) {
    uint32_t hl = HL, r = hl - v - (F & CF);
    WZ = (uint16_t)(hl + 1);
    SETF(((r >> 8) & (SF | YF | XF)) | ((r & 0xffff) ? 0 : ZF) | (((hl ^ v ^ r) >> 8) & HF) |
         ((((hl ^ v) & (hl ^ r)) & 0x8000) >> 13) | NF | ((r >> 16) & CF));
    HL = (uint16_t)r;
}
/* rotaciones y desplazamientos del prefijo CB (op = bits 3-5 del opcode) */
static inline uint8_t alu_rot(Z80 *z, int op, uint8_t v) {
    uint8_t r, c;
    switch (op) {
    case 0: c = v >> 7; r = (uint8_t)((v << 1) | c); break;               /* RLC */
    case 1: c = v & 1; r = (uint8_t)((v >> 1) | (v << 7)); break;         /* RRC */
    case 2: c = v >> 7; r = (uint8_t)((v << 1) | (F & CF)); break;        /* RL */
    case 3: c = v & 1; r = (uint8_t)((v >> 1) | ((F & CF) << 7)); break;  /* RR */
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;                     /* SLA */
    case 5: c = v & 1; r = (uint8_t)((v >> 1) | (v & 0x80)); break;       /* SRA */
    case 6: c = v >> 7; r = (uint8_t)((v << 1) | 1); break;               /* SLL (no documentada) */
    default: c = v & 1; r = (uint8_t)(v >> 1); break;                     /* SRL */
    }
    SETF(sz53p[r] | c);
    return r;
}
/* BIT: X/Y de `xy` (el registro, MEMPTR alto con (HL) o la dirección alta con (IX+d)) */
static inline void alu_bit(Z80 *z, int bit, uint8_t v, uint8_t xy) {
    uint8_t r = (uint8_t)(v & (1u << bit));
    SETF((F & CF) | HF | (r & SF) | (r ? 0 : (ZF | PF)) | (xy & (YF | XF)));
}
static inline void alu_daa(Z80 *z) {
    uint8_t a = A;
    if (F & NF) {
        if ((F & HF) || (A & 0x0f) > 9) a -= 6;
        if ((F & CF) || A > 0x99) a -= 0x60;
    } else {
        if ((F & HF) || (A & 0x0f) > 9) a += 6;
        if ((F & CF) || A > 0x99) a += 0x60;
    }
    SETF((F & NF) | sz53p[a] | ((A ^ a) & HF) | (((F & CF) || A > 0x99) ? CF : 0));
    A = a;
}

static inline int cond(Z80 *z, int cc) {
    switch (cc) {
    case 0: return !(F & ZF);
    case 1: return (F & ZF) != 0;
    case 2: return !(F & CF);
    case 3: return (F & CF) != 0;
    case 4: return !(F & PF);
    case 5: return (F & PF) != 0;
    case 6: return !(F & SF);
    default: return (F & SF) != 0;
    }
}

/* registro de 8 bits por código (0 B, 1 C, 2 D, 3 E, 4 H, 5 L, 7 A); con prefijo DD/FD, H y L son la mitad
 * alta y baja de IX/IY (xp), salvo en las instrucciones que usan (IX+d), que pasan &z->hl */
static inline uint8_t getr(Z80 *z, int code, Z80Pair *xp) {
    switch (code) {
    case 0: return B; case 1: return C; case 2: return D; case 3: return E;
    case 4: return xp->b.h; case 5: return xp->b.l; default: return A;
    }
}
static inline void setr(Z80 *z, int code, Z80Pair *xp, uint8_t v) {
    switch (code) {
    case 0: B = v; break; case 1: C = v; break; case 2: D = v; break; case 3: E = v; break;
    case 4: xp->b.h = v; break; case 5: xp->b.l = v; break; default: A = v; break;
    }
}
/* par de 16 bits por código (0 BC, 1 DE, 2 HL/IX/IY, 3 SP) */
static inline uint16_t getrp(Z80 *z, int code, Z80Pair *xp) {
    switch (code) { case 0: return BC; case 1: return DE; case 2: return xp->w; default: return SP; }
}
static inline void setrp(Z80 *z, int code, Z80Pair *xp, uint16_t v) {
    switch (code) { case 0: BC = v; break; case 1: DE = v; break; case 2: xp->w = v; break; default: SP = v; break; }
}

/* ---- prefijo ED -------------------------------------------------------------------------- */
static void exec_ed(Z80 *z, int *tp) {
    int t = *tp;
    uint8_t op = M1();
    int y = (op >> 3) & 7;
    if (op >= 0x40 && op < 0x80) {
        switch (op & 7) {
        case 0: {                                           /* IN r,(C) */
            uint8_t v = IN(BC);
            SETF((F & CF) | sz53p[v]);
            WZ = (uint16_t)(BC + 1);
            if (y != 6) setr(z, y, &z->hl, v);
            break;
        }
        case 1:                                             /* OUT (C),r */
            OUT(BC, y == 6 ? 0 : getr(z, y, &z->hl));
            WZ = (uint16_t)(BC + 1);
            break;
        case 2:                                             /* SBC/ADC HL,rr */
            IDLE(7);
            if (y & 1) alu_adc16(z, getrp(z, y >> 1, &z->hl));
            else alu_sbc16(z, getrp(z, y >> 1, &z->hl));
            break;
        case 3: {                                           /* LD (nn),rr / LD rr,(nn) */
            uint16_t ea = ARG16();
            if (y & 1) {
                uint16_t lo = RD(ea), hi = RD(ea + 1);
                setrp(z, y >> 1, &z->hl, (uint16_t)(lo | (hi << 8)));
            } else {
                uint16_t v = getrp(z, y >> 1, &z->hl);
                WR(ea, v & 0xff);
                WR(ea + 1, v >> 8);
            }
            WZ = (uint16_t)(ea + 1);
            break;
        }
        case 4: {                                           /* NEG */
            uint8_t v = A;
            A = 0;
            alu_sub(z, v, 0);
            break;
        }
        case 5:                                             /* RETN / RETI */
            PC = POP16();
            WZ = PC;
            z->iff1 = z->iff2;
            break;
        case 6:                                             /* IM */
            z->im = (y & 3) == 2 ? 1 : (y & 3) == 3 ? 2 : 0;
            break;
        default:
            switch (y) {
            case 0: IDLE(1); z->i = A; break;                                   /* LD I,A */
            case 1: IDLE(1); z->r = A; z->r2 = A & 0x80; break;                 /* LD R,A */
            case 2: IDLE(1); A = z->i; SETF((F & CF) | sz53[A] | (z->iff2 ? PF : 0)); break;   /* LD A,I */
            case 3: IDLE(1); A = (uint8_t)((z->r & 0x7f) | z->r2); SETF((F & CF) | sz53[A] | (z->iff2 ? PF : 0)); break;
            case 4: {                                                           /* RRD */
                uint8_t v = RD(HL);
                WZ = (uint16_t)(HL + 1);
                IDLE(4);
                WR(HL, (v >> 4) | (A << 4));
                A = (uint8_t)((A & 0xf0) | (v & 0x0f));
                SETF((F & CF) | sz53p[A]);
                break;
            }
            case 5: {                                                           /* RLD */
                uint8_t v = RD(HL);
                WZ = (uint16_t)(HL + 1);
                IDLE(4);
                WR(HL, (v << 4) | (A & 0x0f));
                A = (uint8_t)((A & 0xf0) | (v >> 4));
                SETF((F & CF) | sz53p[A]);
                break;
            }
            default: break;                                                     /* ED 77 / ED 7F: NOP */
            }
        }
    } else if (op >= 0xa0 && op < 0xc0 && (op & 7) < 4 && y >= 4) {
        int dec = y & 1, rep = y & 2;
        switch (op & 3) {
        case 0: {                                           /* LDI LDD LDIR LDDR */
            uint8_t v = RD(HL);
            WR(DE, v);
            IDLE(2);
            HL = (uint16_t)(dec ? HL - 1 : HL + 1);
            DE = (uint16_t)(dec ? DE - 1 : DE + 1);
            BC--;
            uint8_t n = (uint8_t)(A + v);
            SETF((F & (SF | ZF | CF)) | (BC ? PF : 0) | (n & XF) | ((n << 4) & YF));
            if (rep && BC) {
                IDLE(5);
                PC -= 2;
                WZ = (uint16_t)(PC + 1);
                SETF((F & ~(YF | XF)) | ((PC >> 8) & (YF | XF)));
            }
            break;
        }
        case 1: {                                           /* CPI CPD CPIR CPDR */
            uint8_t v = RD(HL);
            IDLE(5);
            WZ = (uint16_t)(dec ? WZ - 1 : WZ + 1);
            HL = (uint16_t)(dec ? HL - 1 : HL + 1);
            BC--;
            uint8_t r = (uint8_t)(A - v);
            uint8_t f = (uint8_t)((F & CF) | NF | (sz53[r] & (SF | ZF)) | ((A ^ v ^ r) & HF) | (BC ? PF : 0));
            uint8_t n = (uint8_t)(r - ((f & HF) ? 1 : 0));
            SETF(f | (n & XF) | ((n << 4) & YF));
            if (rep && BC && !(F & ZF)) {
                IDLE(5);
                PC -= 2;
                WZ = (uint16_t)(PC + 1);
                SETF((F & ~(YF | XF)) | ((PC >> 8) & (YF | XF)));
            }
            break;
        }
        case 2: {                                           /* INI IND INIR INDR */
            IDLE(1);
            uint8_t v = IN(BC);
            WZ = (uint16_t)(dec ? BC - 1 : BC + 1);
            B--;
            WR(HL, v);
            HL = (uint16_t)(dec ? HL - 1 : HL + 1);
            unsigned k = (unsigned)(uint8_t)(dec ? C - 1 : C + 1) + v;
            SETF(sz53[B] | ((v & 0x80) ? NF : 0) | (k > 255 ? (HF | CF) : 0) | (sz53p[(k & 7) ^ B] & PF));
            if (rep && B) { IDLE(5); PC -= 2; }
            break;
        }
        default: {                                          /* OUTI OUTD OTIR OTDR */
            IDLE(1);
            uint8_t v = RD(HL);
            B--;
            WZ = (uint16_t)(dec ? BC - 1 : BC + 1);
            OUT(BC, v);
            HL = (uint16_t)(dec ? HL - 1 : HL + 1);
            unsigned k = (unsigned)L + v;
            SETF(sz53[B] | ((v & 0x80) ? NF : 0) | (k > 255 ? (HF | CF) : 0) | (sz53p[(k & 7) ^ B] & PF));
            if (rep && B) { IDLE(5); PC -= 2; }
            break;
        }
        }
    }
    /* el resto del prefijo ED es un NOP de 8 ciclos */
    *tp = t;
}

/* ---- prefijo CB (y DD CB / FD CB) ---------------------------------------------------------- */
static void exec_cb(Z80 *z, int *tp, Z80Pair *xp, int indexed) {
    int t = *tp;
    if (!indexed) {
        uint8_t op = M1();
        int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
        if (r == 6) {
            uint8_t v = RD(HL);
            IDLE(1);
            if (x == 1) alu_bit(z, y, v, z->wz.b.h);
            else {
                v = x == 0 ? alu_rot(z, y, v) : x == 2 ? (uint8_t)(v & ~(1u << y)) : (uint8_t)(v | (1u << y));
                WR(HL, v);
            }
        } else {
            uint8_t v = getr(z, r, &z->hl);
            if (x == 1) alu_bit(z, y, v, v);
            else {
                v = x == 0 ? alu_rot(z, y, v) : x == 2 ? (uint8_t)(v & ~(1u << y)) : (uint8_t)(v | (1u << y));
                setr(z, r, &z->hl, v);
            }
        }
    } else {
        /* DD CB d op: desplazamiento y opcode se leen como datos (sin M1), 2 ciclos internos */
        uint16_t ea = (uint16_t)(xp->w + (int8_t)ARG());
        WZ = ea;
        uint8_t op = ARG();
        IDLE(2);
        int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
        uint8_t v = RD(ea);
        IDLE(1);
        if (x == 1) alu_bit(z, y, v, (uint8_t)(ea >> 8));
        else {
            v = x == 0 ? alu_rot(z, y, v) : x == 2 ? (uint8_t)(v & ~(1u << y)) : (uint8_t)(v | (1u << y));
            WR(ea, v);
            if (r != 6) setr(z, r, &z->hl, v);              /* copia no documentada en el registro */
        }
    }
    *tp = t;
}

/* ---- página principal (idx: &z->hl, &z->ix o &z->iy según el prefijo) ------------------------ */
static void exec_main(Z80 *z, int *tp, uint8_t op, Z80Pair *xp) {
    int t = *tp;
    int indexed = xp != &z->hl;
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;

/* dirección de (HL) o (IX+d) con los ciclos internos tras el desplazamiento */
#define EA_MEM(idle) (indexed ? (uint16_t)(WZ = (uint16_t)(xp->w + (int8_t)ARG()), IDLE(idle), WZ) : HL)

    if (x == 1) {
        if (op == 0x76) {                                    /* HALT */
            z->halt = 1;
        } else if (r == 6) {                                 /* LD r,(HL) */
            uint16_t ea = EA_MEM(5);
            setr(z, y, &z->hl, RD(ea));
        } else if (y == 6) {                                 /* LD (HL),r */
            uint16_t ea = EA_MEM(5);
            WR(ea, getr(z, r, &z->hl));
        } else {
            setr(z, y, xp, getr(z, r, xp));
        }
        *tp = t;
        return;
    }
    if (x == 2) {                                            /* ALU A,r */
        if (r == 6) {
            uint16_t ea = EA_MEM(5);
            alu_op(z, y, RD(ea));
        } else {
            alu_op(z, y, getr(z, r, xp));
        }
        *tp = t;
        return;
    }

    switch (op) {
    case 0x00: break;                                                            /* NOP */
    case 0x01: case 0x11: case 0x21: case 0x31: setrp(z, y >> 1, xp, ARG16()); break;   /* LD rr,nn */
    case 0x02: WR(BC, A); z->wz.b.l = (uint8_t)(BC + 1); z->wz.b.h = A; break;    /* LD (BC),A */
    case 0x12: WR(DE, A); z->wz.b.l = (uint8_t)(DE + 1); z->wz.b.h = A; break;    /* LD (DE),A */
    case 0x0a: A = RD(BC); WZ = (uint16_t)(BC + 1); break;                       /* LD A,(BC) */
    case 0x1a: A = RD(DE); WZ = (uint16_t)(DE + 1); break;                       /* LD A,(DE) */
    case 0x03: case 0x13: case 0x23: case 0x33:                                 /* INC rr */
        IDLE(2); setrp(z, y >> 1, xp, (uint16_t)(getrp(z, y >> 1, xp) + 1)); break;
    case 0x0b: case 0x1b: case 0x2b: case 0x3b:                                 /* DEC rr */
        IDLE(2); setrp(z, y >> 1, xp, (uint16_t)(getrp(z, y >> 1, xp) - 1)); break;
    case 0x04: case 0x0c: case 0x14: case 0x1c: case 0x24: case 0x2c: case 0x3c: /* INC r */
        setr(z, y, xp, alu_inc(z, getr(z, y, xp))); break;
    case 0x05: case 0x0d: case 0x15: case 0x1d: case 0x25: case 0x2d: case 0x3d: /* DEC r */
        setr(z, y, xp, alu_dec(z, getr(z, y, xp))); break;
    case 0x34: case 0x35: {                                                      /* INC/DEC (HL) */
        uint16_t ea = EA_MEM(5);
        uint8_t v = RD(ea);
        IDLE(1);
        WR(ea, op == 0x34 ? alu_inc(z, v) : alu_dec(z, v));
        break;
    }
    case 0x06: case 0x0e: case 0x16: case 0x1e: case 0x26: case 0x2e: case 0x3e: /* LD r,n */
        setr(z, y, xp, ARG()); break;
    case 0x36: {                                                                 /* LD (HL),n */
        if (indexed) {
            uint16_t ea = (uint16_t)(xp->w + (int8_t)ARG());
            WZ = ea;
            uint8_t n = ARG();
            IDLE(2);
            WR(ea, n);
        } else {
            uint8_t n = ARG();
            WR(HL, n);
        }
        break;
    }
    case 0x07: A = (uint8_t)((A << 1) | (A >> 7)); SETF((F & (SF | ZF | PF)) | (A & (YF | XF | CF))); break;  /* RLCA */
    case 0x0f: SETF((F & (SF | ZF | PF)) | (A & CF)); A = (uint8_t)((A >> 1) | (A << 7)); F |= A & (YF | XF); break; /* RRCA */
    case 0x17: { uint8_t c = A >> 7; A = (uint8_t)((A << 1) | (F & CF)); SETF((F & (SF | ZF | PF)) | (A & (YF | XF)) | c); break; } /* RLA */
    case 0x1f: { uint8_t c = A & 1; A = (uint8_t)((A >> 1) | (F << 7)); SETF((F & (SF | ZF | PF)) | (A & (YF | XF)) | c); break; }  /* RRA */
    case 0x08: { uint16_t v = z->af.w; z->af.w = z->af2.w; z->af2.w = v; break; } /* EX AF,AF' */
    case 0x09: case 0x19: case 0x29: case 0x39:                                 /* ADD HL,rr */
        IDLE(7); xp->w = alu_add16(z, xp->w, getrp(z, y >> 1, xp)); break;
    case 0x10: {                                                                 /* DJNZ */
        IDLE(1);
        B--;
        int8_t d = (int8_t)ARG();
        if (B) { IDLE(5); PC = (uint16_t)(PC + d); WZ = PC; }
        break;
    }
    case 0x18: { int8_t d = (int8_t)ARG(); IDLE(5); PC = (uint16_t)(PC + d); WZ = PC; break; }     /* JR */
    case 0x20: case 0x28: case 0x30: case 0x38: {                               /* JR cc */
        int8_t d = (int8_t)ARG();
        if (cond(z, y - 4)) { IDLE(5); PC = (uint16_t)(PC + d); WZ = PC; }
        break;
    }
    case 0x22: {                                                                 /* LD (nn),HL */
        uint16_t ea = ARG16();
        WR(ea, xp->b.l); WR(ea + 1, xp->b.h);
        WZ = (uint16_t)(ea + 1);
        break;
    }
    case 0x2a: {                                                                 /* LD HL,(nn) */
        uint16_t ea = ARG16();
        uint8_t lo = RD(ea), hi = RD(ea + 1);
        xp->b.l = lo; xp->b.h = hi;
        WZ = (uint16_t)(ea + 1);
        break;
    }
    case 0x32: {                                                                 /* LD (nn),A */
        uint16_t ea = ARG16();
        WR(ea, A);
        z->wz.b.l = (uint8_t)(ea + 1); z->wz.b.h = A;
        break;
    }
    case 0x3a: { uint16_t ea = ARG16(); A = RD(ea); WZ = (uint16_t)(ea + 1); break; }          /* LD A,(nn) */
    case 0x27: alu_daa(z); break;                                                /* DAA */
    case 0x2f: A ^= 0xff; SETF((F & (SF | ZF | PF | CF)) | HF | NF | (A & (YF | XF))); break;  /* CPL */
    case 0x37: SETF((F & (SF | ZF | PF)) | (((F & z->q) | A) & (YF | XF)) | CF); break;        /* SCF */
    case 0x3f: { uint8_t f = F; SETF((f & (SF | ZF | PF)) | (((f & z->q) | A) & (YF | XF)) | ((f & CF) << 4) | ((f & CF) ^ CF)); break; } /* CCF */

    case 0xc0: case 0xc8: case 0xd0: case 0xd8: case 0xe0: case 0xe8: case 0xf0: case 0xf8: /* RET cc */
        IDLE(1);
        if (cond(z, y)) { PC = POP16(); WZ = PC; }
        break;
    case 0xc9: PC = POP16(); WZ = PC; break;                                      /* RET */
    case 0xc1: case 0xd1: case 0xe1: setrp(z, y >> 1, xp, POP16()); break;        /* POP rr */
    case 0xf1: z->af.w = POP16(); break;                                          /* POP AF */
    case 0xc5: case 0xd5: case 0xe5: IDLE(1); PUSH16(getrp(z, y >> 1, xp)); break; /* PUSH rr */
    case 0xf5: IDLE(1); PUSH16(z->af.w); break;                                   /* PUSH AF */
    case 0xc2: case 0xca: case 0xd2: case 0xda: case 0xe2: case 0xea: case 0xf2: case 0xfa: { /* JP cc,nn */
        uint16_t nn = ARG16();
        WZ = nn;
        if (cond(z, y)) PC = nn;
        break;
    }
    case 0xc3: PC = ARG16(); WZ = PC; break;                                      /* JP nn */
    case 0xc4: case 0xcc: case 0xd4: case 0xdc: case 0xe4: case 0xec: case 0xf4: case 0xfc: { /* CALL cc,nn */
        uint16_t nn = ARG16();
        WZ = nn;
        if (cond(z, y)) { IDLE(1); PUSH16(PC); PC = nn; }
        break;
    }
    case 0xcd: { uint16_t nn = ARG16(); IDLE(1); WZ = nn; PUSH16(PC); PC = nn; break; }      /* CALL nn */
    case 0xc6: case 0xce: case 0xd6: case 0xde: case 0xe6: case 0xee: case 0xf6: case 0xfe: /* ALU A,n */
        alu_op(z, y, ARG()); break;
    case 0xc7: case 0xcf: case 0xd7: case 0xdf: case 0xe7: case 0xef: case 0xf7: case 0xff: /* RST */
        IDLE(1); PUSH16(PC); PC = (uint16_t)(op & 0x38); WZ = PC; break;
    case 0xd3: {                                                                 /* OUT (n),A */
        uint8_t n = ARG();
        uint16_t port = (uint16_t)(n | (A << 8));
        OUT(port, A);
        z->wz.b.l = (uint8_t)(n + 1); z->wz.b.h = A;
        break;
    }
    case 0xdb: {                                                                 /* IN A,(n) */
        uint8_t n = ARG();
        uint16_t port = (uint16_t)(n | (A << 8));
        A = IN(port);
        WZ = (uint16_t)(port + 1);
        break;
    }
    case 0xd9: {                                                                 /* EXX */
        uint16_t v;
        v = BC; BC = z->bc2.w; z->bc2.w = v;
        v = DE; DE = z->de2.w; z->de2.w = v;
        v = HL; HL = z->hl2.w; z->hl2.w = v;
        break;
    }
    case 0xe3: {                                                                 /* EX (SP),HL */
        uint16_t v = POP16();
        IDLE(1);
        PUSH16(xp->w);
        IDLE(2);
        xp->w = v;
        WZ = v;
        break;
    }
    case 0xe9: PC = xp->w; break;                                                 /* JP (HL) */
    case 0xeb: { uint16_t v = DE; DE = HL; HL = v; break; }                        /* EX DE,HL (sin prefijo) */
    case 0xf3: z->iff1 = z->iff2 = 0; break;                                      /* DI */
    case 0xfb: z->iff1 = z->iff2 = 1; z->after_ei = 1; break;                     /* EI */
    case 0xf9: IDLE(2); SP = xp->w; break;                                        /* LD SP,HL */
    case 0xcb: exec_cb(z, &t, xp, indexed); break;
    case 0xed: exec_ed(z, &t); break;
    default: break;
    }
#undef EA_MEM
    *tp = t;
}

/* ---- API ------------------------------------------------------------------------------------ */
void z80_reset(Z80 *z) {
    Z80Bus bus = z->bus;
    if (!tables_ready)
        init_tables();
    memset(z, 0, sizeof *z);
    z->bus = bus;
    z->q = 0x28;
}

void z80_state(Z80 *z, StateStream *s) {
    Z80Pair *pairs[] = { &z->af, &z->bc, &z->de, &z->hl, &z->ix, &z->iy, &z->wz, &z->af2, &z->bc2, &z->de2, &z->hl2 };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
        st_u16(s, &pairs[i]->w);
    st_u16(s, &z->sp);
    st_u16(s, &z->pc);
    st_u8(s, &z->i); st_u8(s, &z->r); st_u8(s, &z->r2);
    uint8_t *bits[] = { &z->iff1, &z->iff2, &z->halt, &z->after_ei, &z->irq_line, &z->qset };
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        st_u8(s, bits[i]);
        st_check(s, *bits[i] <= 1);
    }
    st_u8(s, &z->im);
    st_check(s, z->im <= 2);
    st_u8(s, &z->q);
    st_u64(s, &z->cycles);
    st_u64(s, &z->insns);
    st_u64(s, &z->irqs);
}

void z80_set_irq(Z80 *z, int level) {
    z->irq_line = level ? 1 : 0;
}

int z80_step(Z80 *z) {
    int t = 0;
    /* límite de instrucción: INT por nivel con IFF1, salvo justo después de EI */
    if (z->irq_line && z->iff1 && !z->after_ei) {
        z->after_ei = 0;
        z->halt = 0;
        z->iff1 = z->iff2 = 0;
        z->r++;
        z->irqs++;
        IDLE(7);                                    /* ciclo de reconocimiento (latencia + RST) */
        PUSH16(PC);
        if (z->im == 2) {
            uint16_t vec = (uint16_t)((z->i << 8) | 0xff);
            uint8_t lo = RD(vec), hi = RD(vec + 1);
            PC = (uint16_t)(lo | (hi << 8));
        } else {
            PC = 0x38;                              /* IM 1, o IM 0 con 0xFF en el bus (RST 38h) */
        }
        WZ = PC;
        z->cycles += (uint64_t)t;
        return t;
    }
    z->after_ei = 0;
    if (z->halt) {
        (void)z->bus.fetch(z->bus.ctx, PC, z->cycles);   /* M1 sobre la instrucción siguiente, descartado */
        z->r++;
        z->cycles += 4;
        return 4;
    }
    uint8_t op = M1();
    Z80Pair *xp = &z->hl;
    while (op == 0xdd || op == 0xfd) {
        xp = op == 0xdd ? &z->ix : &z->iy;
        op = M1();
    }
    if (op == 0xed)
        exec_ed(z, &t);
    else
        exec_main(z, &t, op, xp);
    z->q = z->qset ? 0 : 0x28;
    z->qset = 0;
    z->insns++;
    z->cycles += (uint64_t)t;
    return t;
}

/* ---- desensamblador ------------------------------------------------------------------------ */
static const char *const r8n[] = { "b", "c", "d", "e", "h", "l", "(hl)", "a" };
static const char *const rpn[] = { "bc", "de", "hl", "sp" };
static const char *const rp2n[] = { "bc", "de", "hl", "af" };
static const char *const ccn[] = { "nz", "z", "nc", "c", "po", "pe", "p", "m" };
static const char *const alun[] = { "add a,", "adc a,", "sub ", "sbc a,", "and ", "xor ", "or ", "cp " };
static const char *const rotn[] = { "rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl" };

int z80_disasm(uint16_t pc, uint8_t (*peek)(void *ctx, uint16_t addr), void *ctx, char *buf, size_t n) {
    uint16_t p = pc;
#define NB() peek(ctx, p++)
    uint8_t op = NB();
    const char *ix = NULL;
    char r8[8][16];
    for (int i = 0; i < 8; i++) snprintf(r8[i], sizeof r8[i], "%s", r8n[i]);
    while (op == 0xdd || op == 0xfd) {
        ix = op == 0xdd ? "ix" : "iy";
        op = NB();
    }
    int d = 0;
    const char *hl = ix ? ix : "hl";
    if (op == 0xcb) {
        if (ix) d = (int8_t)NB();
        uint8_t o = NB();
        int x = o >> 6, y = (o >> 3) & 7, r = o & 7;
        char m[24];
        if (ix) snprintf(m, sizeof m, "(%s%+d)", ix, d); else snprintf(m, sizeof m, "%s", r8n[r]);
        const char *opd = (ix || r == 6) ? m : r8n[r];
        if (x == 0) snprintf(buf, n, "%s %s", rotn[y], opd);
        else snprintf(buf, n, "%s %d,%s", x == 1 ? "bit" : x == 2 ? "res" : "set", y, opd);
        return (uint16_t)(p - pc);
    }
    if (op == 0xed) {
        uint8_t o = NB();
        int y = (o >> 3) & 7;
        if (o >= 0x40 && o < 0x80) {
            switch (o & 7) {
            case 0: snprintf(buf, n, y == 6 ? "in (c)" : "in %s,(c)", r8n[y]); break;
            case 1: snprintf(buf, n, y == 6 ? "out (c),0" : "out (c),%s", r8n[y]); break;
            case 2: snprintf(buf, n, "%s hl,%s", (y & 1) ? "adc" : "sbc", rpn[y >> 1]); break;
            case 3: { uint16_t nn = NB(); nn |= (uint16_t)(NB() << 8);
                      if (y & 1) snprintf(buf, n, "ld %s,($%04x)", rpn[y >> 1], nn);
                      else snprintf(buf, n, "ld ($%04x),%s", nn, rpn[y >> 1]); break; }
            case 4: snprintf(buf, n, "neg"); break;
            case 5: snprintf(buf, n, y == 1 ? "reti" : "retn"); break;
            case 6: snprintf(buf, n, "im %d", (y & 3) == 2 ? 1 : (y & 3) == 3 ? 2 : 0); break;
            default: {
                static const char *const m[] = { "ld i,a", "ld r,a", "ld a,i", "ld a,r", "rrd", "rld", "nop", "nop" };
                snprintf(buf, n, "%s", m[y]);
            }
            }
        } else if (o >= 0xa0 && o < 0xc0 && (o & 7) < 4 && y >= 4) {
            static const char *const m[4][4] = { { "ldi", "cpi", "ini", "outi" }, { "ldd", "cpd", "ind", "outd" },
                                                 { "ldir", "cpir", "inir", "otir" }, { "lddr", "cpdr", "indr", "otdr" } };
            snprintf(buf, n, "%s", m[y - 4][o & 3]);
        } else {
            snprintf(buf, n, "db $ed,$%02x", o);
        }
        return (uint16_t)(p - pc);
    }
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    char mem[24];
    int uses_mem = (x == 1 && (r == 6 || y == 6) && op != 0x76) || (x == 2 && r == 6) || op == 0x34 || op == 0x35 || op == 0x36;
    if (ix && uses_mem) { d = (int8_t)NB(); snprintf(mem, sizeof mem, "(%s%+d)", ix, d); }
    else snprintf(mem, sizeof mem, "(hl)");
    if (ix) {
        snprintf(r8[4], sizeof r8[4], "%sh", ix);
        snprintf(r8[5], sizeof r8[5], "%sl", ix);
    }
    if (uses_mem) { snprintf(r8[4], sizeof r8[4], "h"); snprintf(r8[5], sizeof r8[5], "l"); }
    snprintf(r8[6], sizeof r8[6], "%s", mem);
    if (x == 1) {
        if (op == 0x76) snprintf(buf, n, "halt");
        else snprintf(buf, n, "ld %s,%s", r8[y], r8[r]);
        return (uint16_t)(p - pc);
    }
    if (x == 2) { snprintf(buf, n, "%s%s", alun[y], r8[r]); return (uint16_t)(p - pc); }
    uint16_t nn;
    int8_t e;
    switch (op) {
    case 0x00: snprintf(buf, n, "nop"); break;
    case 0x01: case 0x11: case 0x21: case 0x31: nn = NB(); nn |= (uint16_t)(NB() << 8);
        snprintf(buf, n, "ld %s,$%04x", y >> 1 == 2 ? hl : rpn[y >> 1], nn); break;
    case 0x02: snprintf(buf, n, "ld (bc),a"); break;
    case 0x12: snprintf(buf, n, "ld (de),a"); break;
    case 0x0a: snprintf(buf, n, "ld a,(bc)"); break;
    case 0x1a: snprintf(buf, n, "ld a,(de)"); break;
    case 0x03: case 0x13: case 0x23: case 0x33: snprintf(buf, n, "inc %s", y >> 1 == 2 ? hl : rpn[y >> 1]); break;
    case 0x0b: case 0x1b: case 0x2b: case 0x3b: snprintf(buf, n, "dec %s", y >> 1 == 2 ? hl : rpn[y >> 1]); break;
    case 0x04: case 0x0c: case 0x14: case 0x1c: case 0x24: case 0x2c: case 0x34: case 0x3c: snprintf(buf, n, "inc %s", r8[y]); break;
    case 0x05: case 0x0d: case 0x15: case 0x1d: case 0x25: case 0x2d: case 0x35: case 0x3d: snprintf(buf, n, "dec %s", r8[y]); break;
    case 0x06: case 0x0e: case 0x16: case 0x1e: case 0x26: case 0x2e: case 0x36: case 0x3e:
        snprintf(buf, n, "ld %s,$%02x", r8[y], NB()); break;
    case 0x07: snprintf(buf, n, "rlca"); break;
    case 0x0f: snprintf(buf, n, "rrca"); break;
    case 0x17: snprintf(buf, n, "rla"); break;
    case 0x1f: snprintf(buf, n, "rra"); break;
    case 0x08: snprintf(buf, n, "ex af,af'"); break;
    case 0x09: case 0x19: case 0x29: case 0x39: snprintf(buf, n, "add %s,%s", hl, y >> 1 == 2 ? hl : rpn[y >> 1]); break;
    case 0x10: e = (int8_t)NB(); snprintf(buf, n, "djnz $%04x", (uint16_t)(p + e)); break;
    case 0x18: e = (int8_t)NB(); snprintf(buf, n, "jr $%04x", (uint16_t)(p + e)); break;
    case 0x20: case 0x28: case 0x30: case 0x38: e = (int8_t)NB(); snprintf(buf, n, "jr %s,$%04x", ccn[y - 4], (uint16_t)(p + e)); break;
    case 0x22: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "ld ($%04x),%s", nn, hl); break;
    case 0x2a: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "ld %s,($%04x)", hl, nn); break;
    case 0x32: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "ld ($%04x),a", nn); break;
    case 0x3a: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "ld a,($%04x)", nn); break;
    case 0x27: snprintf(buf, n, "daa"); break;
    case 0x2f: snprintf(buf, n, "cpl"); break;
    case 0x37: snprintf(buf, n, "scf"); break;
    case 0x3f: snprintf(buf, n, "ccf"); break;
    case 0xc9: snprintf(buf, n, "ret"); break;
    case 0xc3: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "jp $%04x", nn); break;
    case 0xcd: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "call $%04x", nn); break;
    case 0xd3: snprintf(buf, n, "out ($%02x),a", NB()); break;
    case 0xdb: snprintf(buf, n, "in a,($%02x)", NB()); break;
    case 0xd9: snprintf(buf, n, "exx"); break;
    case 0xe3: snprintf(buf, n, "ex (sp),%s", hl); break;
    case 0xe9: snprintf(buf, n, "jp (%s)", hl); break;
    case 0xeb: snprintf(buf, n, "ex de,hl"); break;
    case 0xf3: snprintf(buf, n, "di"); break;
    case 0xfb: snprintf(buf, n, "ei"); break;
    case 0xf9: snprintf(buf, n, "ld sp,%s", hl); break;
    default:
        switch (op & 7) {
        case 0: snprintf(buf, n, "ret %s", ccn[y]); break;
        case 1: snprintf(buf, n, "pop %s", y >> 1 == 2 ? hl : rp2n[y >> 1]); break;
        case 2: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "jp %s,$%04x", ccn[y], nn); break;
        case 4: nn = NB(); nn |= (uint16_t)(NB() << 8); snprintf(buf, n, "call %s,$%04x", ccn[y], nn); break;
        case 5: snprintf(buf, n, "push %s", y >> 1 == 2 ? hl : rp2n[y >> 1]); break;
        case 6: snprintf(buf, n, "%s$%02x", alun[y], NB()); break;
        case 7: snprintf(buf, n, "rst $%02x", op & 0x38); break;
        default: snprintf(buf, n, "db $%02x", op); break;
        }
    }
#undef NB
    return (uint16_t)(p - pc);
}
