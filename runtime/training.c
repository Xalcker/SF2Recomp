/* training.c - mod de entrenamiento (ver training.h). */
#include "training.h"
#include <ctype.h>
#include <string.h>
#include "training_priv.h"

/* RAM de trabajo del juego (docs/bitacora/2026-10-19-hito6-secuencias.md y docs/ENTRENAMIENTO.md): la de la variante.  El
 * entrenamiento usa m->game->training (D74; en WW: modo 0xFF8001, fase 0xFF8005, subfase 0xFF8009, créditos 0xFF82DD, temporizador
 * 0xFF8ACE en BCD y su subcontador 0xFF8ACF de 30 a 1, jugadores 0xFF83C0 y 0xFF86C0); las cajas, m->game->mod_ram (D68: cámara
 * 0xFF8BD8/DC, proyectiles 0xFF938A) */
#define TR(m)         ((m)->game->training)

#define REL_FWD  0x1000u             /* en un paso del guion: dirección relativa al rival, se resuelve con la orientación */
#define REL_BACK 0x2000u
#define DIR_MASK (SF2_PAD_RIGHT | SF2_PAD_LEFT | SF2_PAD_DOWN | SF2_PAD_UP)

static uint8_t rd8(const Mem *m, uint32_t a) { return m->wram[a - 0xFF0000u]; }
static uint16_t rd16(const Mem *m, uint32_t a) { return (uint16_t)((rd8(m, a) << 8) | rd8(m, a + 1)); }
static void wr8(Mem *m, uint32_t a, uint8_t v) { m->wram[a - 0xFF0000u] = v; }
static void wr16(Mem *m, uint32_t a, uint16_t v) { wr8(m, a, (uint8_t)(v >> 8)); wr8(m, a + 1, (uint8_t)v); }

/* ---- conversión de las entradas de la placa a la forma canónica y de vuelta ------------------------------------------- */
uint16_t training_canon(int player, uint16_t in1, uint16_t in2) {
    return player ? (uint16_t)(((in1 >> 8) & 0x7f) | (((in2 >> 4) & 7) << 7)) : (uint16_t)((in1 & 0x7f) | ((in2 & 7) << 7));
}

static void canon_to_hw(int player, uint16_t c, uint16_t *in1, uint16_t *in2) {
    uint16_t a = c & 0x7f, k = (uint16_t)((c >> 7) & 7);
    if (player) { *in1 = (uint16_t)(a << 8); *in2 = (uint16_t)(k << 4); }
    else { *in1 = a; *in2 = k; }
}

/* ---- el guion del dummy ----------------------------------------------------------------------------------------------- */
static int parse_part(const char *s, size_t n, uint16_t *bits) {
    static const struct { const char *name; uint16_t bits; } names[] = {
        { "n", 0 }, { "neutral", 0 }, { "wait", 0 },
        { "f", REL_FWD }, { "fwd", REL_FWD }, { "forward", REL_FWD },
        { "b", REL_BACK }, { "back", REL_BACK },
        { "u", SF2_PAD_UP }, { "up", SF2_PAD_UP }, { "d", SF2_PAD_DOWN }, { "down", SF2_PAD_DOWN },
        { "df", REL_FWD | SF2_PAD_DOWN }, { "db", REL_BACK | SF2_PAD_DOWN },
        { "uf", REL_FWD | SF2_PAD_UP }, { "ub", REL_BACK | SF2_PAD_UP },
        { "l", SF2_PAD_LEFT }, { "r", SF2_PAD_RIGHT },
        { "lp", SF2_PAD_LP }, { "mp", SF2_PAD_MP }, { "hp", SF2_PAD_HP },
        { "lk", SF2_PAD_LK }, { "mk", SF2_PAD_MK }, { "hk", SF2_PAD_HK },
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        size_t l = strlen(names[i].name);
        if (l != n)
            continue;
        size_t k = 0;
        while (k < n && tolower((unsigned char)s[k]) == names[i].name[k]) k++;
        if (k == n) { *bits = names[i].bits; return 0; }
    }
    return -1;
}

static int parse_script(Training *t, const char *text, char *err, size_t errn) {
    int n = 0;
    const char *p = text;
    t->nsteps = 0;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        const char *e = p;
        while (*e && !isspace((unsigned char)*e) && *e != ',') e++;
        if (n >= TRAINING_MAX_STEPS) {
            snprintf(err, errn, "guion demasiado largo (maximo %d pasos)", TRAINING_MAX_STEPS);
            return -1;
        }
        const char *colon = memchr(p, ':', (size_t)(e - p));
        const char *end_bits = colon ? colon : e;
        unsigned frames = 1;
        if (colon) {
            frames = 0;
            const char *d = colon + 1;
            if (d == e) { snprintf(err, errn, "guion: falta el numero de cuadros tras ':'"); return -1; }
            for (; d < e; d++) {
                if (!isdigit((unsigned char)*d)) { snprintf(err, errn, "guion: '%.*s' no es un numero de cuadros", (int)(e - colon - 1), colon + 1); return -1; }
                frames = frames * 10 + (unsigned)(*d - '0');
                if (frames > 3600) { snprintf(err, errn, "guion: un paso dura como mucho 3600 cuadros"); return -1; }
            }
            if (frames == 0) { snprintf(err, errn, "guion: un paso dura al menos 1 cuadro"); return -1; }
        }
        uint16_t bits = 0;
        const char *q = p;
        while (q < end_bits) {
            const char *plus = memchr(q, '+', (size_t)(end_bits - q));
            const char *pe = plus ? plus : end_bits;
            uint16_t b;
            if (pe == q || parse_part(q, (size_t)(pe - q), &b) != 0) {
                snprintf(err, errn, "guion: '%.*s' no es un boton ni una direccion (n f b u d df db uf ub lp mp hp lk mk hk)", (int)(pe - q), q);
                return -1;
            }
            bits |= b;
            q = plus ? plus + 1 : pe;
        }
        t->steps[n].bits = bits;
        t->steps[n].frames = (uint16_t)frames;
        n++;
        p = e;
    }
    t->nsteps = n;
    t->step = 0;
    t->step_left = n ? t->steps[0].frames : 0;
    return 0;
}

void training_init(Training *t) {
    memset(t, 0, sizeof *t);
    t->cfg.refill_delay = 60;
}

int training_configure(Training *t, const Sf2Training *cfg, char *err, size_t errn) {
    Training tmp = *t;
    tmp.cfg = *cfg;
    if (tmp.cfg.refill_delay < 1) tmp.cfg.refill_delay = 1;
    if (tmp.cfg.refill_delay > 600) tmp.cfg.refill_delay = 600;
    if (tmp.cfg.dummy < 0 || tmp.cfg.dummy >= SF2_DUMMY_COUNT) tmp.cfg.dummy = SF2_DUMMY_STAND;
    tmp.cfg.script[sizeof tmp.cfg.script - 1] = 0;
    char msg[160] = "";
    if (parse_script(&tmp, tmp.cfg.script, msg, sizeof msg) != 0) {
        if (err && errn) snprintf(err, errn, "%s", msg);
        return -1;
    }
    if (tmp.cfg.dummy == SF2_DUMMY_SCRIPT && tmp.nsteps == 0) {
        if (err && errn) snprintf(err, errn, "guion vacio: escribe pasos como \"d:30 df+lp:2 n:20\"");
        return -1;
    }
    tmp.life_prev[0] = tmp.life_prev[1] = -2;
    tmp.life_stable[0] = tmp.life_stable[1] = 0;
    *t = tmp;
    return 0;
}

void training_reset(Training *t) {
    t->tick = 0;
    t->dummy_bits = 0;
    t->step = 0;
    t->step_left = t->nsteps ? t->steps[0].frames : 0;
    t->life_prev[0] = t->life_prev[1] = -2;
    t->life_stable[0] = t->life_stable[1] = 0;
    t->sel_ticks = 0;
    t->last_phase = 0xff;
    t->pulse = 0;
    memset(t->hist, 0, sizeof t->hist);
    memset(t->hist_n, 0, sizeof t->hist_n);
    memset(t->now, 0, sizeof t->now);
    t->p1_busy = 0;
    t->p2_right = 0;
}

/* ---- historial de entradas -------------------------------------------------------------------------------------------- */
static void record(Training *t, int player, uint16_t mask) {
    Sf2InputEvent *h = t->hist[player];
    int *n = &t->hist_n[player];
    t->now[player] = mask;
    if (*n > 0 && h[0].mask == mask) {
        if (h[0].frames < 999) h[0].frames++;
        return;
    }
    if (*n < SF2_TRAIN_HISTORY) (*n)++;
    memmove(h + 1, h, (size_t)(*n - 1) * sizeof *h);
    h[0].mask = mask;
    h[0].frames = 1;
}

int training_history(const Training *t, int player, Sf2InputEvent *out, int max) {
    if (player < 0 || player > 1) return 0;
    int n = t->hist_n[player] < max ? t->hist_n[player] : max;
    memcpy(out, t->hist[player], (size_t)n * sizeof *out);
    return n;
}

/* ---- entradas finales que ve el juego --------------------------------------------------------------------------------- */
void training_inputs(const Training *t, uint16_t h0, uint16_t h1, uint16_t h2, uint16_t *o0, uint16_t *o1, uint16_t *o2) {
    *o0 = h0; *o1 = h1; *o2 = h2;
    if (!t->cfg.enabled)
        return;
    if (t->cfg.lock_p2) {
        uint16_t a, b;
        canon_to_hw(1, t->dummy_bits, &a, &b);
        *o1 = (uint16_t)((h1 & 0x00ff) | a);
        *o2 = (uint16_t)((h2 & 0xff8f) | b);          /* patadas de J2 (bits 4-6) fuera; las de J1 se conservan */
    }
    *o0 |= t->pulse;
}

/* ---- un VBlank -------------------------------------------------------------------------------------------------------- */
static uint16_t resolve(uint16_t bits, int face_right) {
    uint16_t out = (uint16_t)(bits & ~(REL_FWD | REL_BACK));
    if (bits & REL_FWD) out |= face_right ? SF2_PAD_RIGHT : SF2_PAD_LEFT;
    if (bits & REL_BACK) out |= face_right ? SF2_PAD_LEFT : SF2_PAD_RIGHT;
    return out;
}

static uint16_t dummy_step(Training *t, int face_right) {
    const int fwd = face_right ? SF2_PAD_RIGHT : SF2_PAD_LEFT, back = face_right ? SF2_PAD_LEFT : SF2_PAD_RIGHT;
    switch (t->cfg.dummy) {
    case SF2_DUMMY_CROUCH: return SF2_PAD_DOWN;
    case SF2_DUMMY_JUMP: return SF2_PAD_UP;
    case SF2_DUMMY_BLOCK_STAND: return t->p1_busy > 0 ? (uint16_t)back : 0;
    case SF2_DUMMY_BLOCK_CROUCH: return t->p1_busy > 0 ? (uint16_t)(back | SF2_PAD_DOWN) : (uint16_t)SF2_PAD_DOWN;
    case SF2_DUMMY_WALK_FWD: return (uint16_t)fwd;
    case SF2_DUMMY_WALK_BACK: return (uint16_t)back;
    case SF2_DUMMY_SCRIPT: {
        if (!t->nsteps) return 0;
        uint16_t b = resolve(t->steps[t->step].bits, face_right);
        if (--t->step_left <= 0) {
            t->step = (t->step + 1) % t->nsteps;
            t->step_left = t->steps[t->step].frames;
        }
        return b;
    }
    default: return 0;
    }
}

static void refill(Training *t, Mem *m) {
    const TrainingRam *r = TR(m);
    const uint32_t base[2] = { r->p1, r->p2 };
    for (int p = 0; p < 2; p++) {
        int life = (int16_t)rd16(m, base[p] + r->life_off);
        if (life <= 0 || life >= r->life_max) {       /* KO o llena: nada que hacer */
            t->life_prev[p] = life;
            t->life_stable[p] = 0;
            continue;
        }
        if (life == t->life_prev[p]) t->life_stable[p]++;
        else { t->life_stable[p] = 0; t->life_prev[p] = life; }
        if (t->cfg.refill == 2 || t->life_stable[p] >= t->cfg.refill_delay) {
            wr16(m, base[p] + r->life_off, (uint16_t)r->life_max);
            for (int k = 0; k < 2; k++)                 /* las barras que la siguen (sfzch) solo bajan: se llenan también */
                if (r->life_bar[k])
                    wr16(m, base[p] + r->life_bar[k], (uint16_t)r->life_max);
            t->life_prev[p] = r->life_max;
            t->life_stable[p] = 0;
        }
    }
}

/* monedas y start para entrar en un versus desde el título, y J2 elige luchador en la selección */
static void auto_versus(Training *t, const Mem *m, int in_game) {
    t->pulse = 0;
    if (!t->cfg.auto_vs || t->tick < 300)
        return;
    if (in_game) {
        t->auto_wait = 0;
        return;
    }
    t->auto_wait++;
    /* ciclo de 48 cuadros: 4 con la tecla pulsada; monedas hasta tener 2 créditos y luego start de los dos */
    int on = (t->auto_wait % 48) < 4;
    if (!on) return;
    if (TR(m)->credits && rd8(m, TR(m)->credits) < 2) t->pulse = 0x0001;    /* COIN1 */
    else t->pulse = 0x0010 | 0x0020;                                  /* START1 y START2 (sin monedas, directamente) */
}

void training_vblank(Training *t, Mem *m, const uint16_t host[3], uint16_t out[3]) {
    t->tick++;
    const TrainingRam *r = TR(m);
    if (!r) {                                           /* variante sin entrenamiento: entradas tal cual (el visor sí funciona) */
        training_inputs(t, host[0], host[1], host[2], &out[0], &out[1], &out[2]);
        if (t->cfg.track_inputs) {
            record(t, 0, training_canon(0, out[1], out[2]));
            record(t, 1, training_canon(1, out[1], out[2]));
        }
        return;
    }
    const int mode = rd8(m, r->mode), phase = rd8(m, r->phase), sub = rd8(m, r->sub);
    const int in_game = mode == r->mode_game;
    const int fight = in_game && phase == r->phase_fight && sub == r->sub_fight && (!r->sub2 || rd8(m, r->sub2) == r->sub2_fight);
    const int p1_x = (int16_t)rd16(m, r->p1 + r->x_off), p2_x = (int16_t)rd16(m, r->p2 + r->x_off);
    if (p2_x < p1_x) t->p2_right = 1;
    else if (p2_x > p1_x) t->p2_right = 0;

    if (t->cfg.enabled) {
        auto_versus(t, m, in_game);
        if (t->cfg.infinite_time && fight) {
            wr8(m, r->timer, r->timer_full);
            wr8(m, r->timer_sub, r->timer_sub_full);    /* sin esto el segundo se cumple igual y el marcador se queda en 98 */
        }
        if (t->cfg.refill && fight)
            refill(t, m);
        /* J1 está atacando (para que el dummy se cubra): algún botón de ataque en los últimos 30 cuadros */
        if (training_canon(0, host[1], host[2]) & (SF2_PAD_LP | SF2_PAD_MP | SF2_PAD_HP | SF2_PAD_LK | SF2_PAD_MK | SF2_PAD_HK)) t->p1_busy = 30;
        else if (t->p1_busy > 0) t->p1_busy--;
        if (phase != t->last_phase) { t->sel_ticks = 0; t->last_phase = (uint8_t)phase; }
        else t->sel_ticks++;
        uint16_t d = 0;
        if (t->cfg.lock_p2) {
            if (fight) d = dummy_step(t, t->p2_right);
            else if (in_game && phase == r->phase_select && t->sel_ticks > 90 && (t->sel_ticks % 30) < 3) d = SF2_PAD_LP;   /* elige el luchador del cursor */
        }
        t->dummy_bits = d;
    } else {
        t->dummy_bits = 0;
        t->pulse = 0;
    }
    uint16_t o0, o1, o2;
    training_inputs(t, host[0], host[1], host[2], &o0, &o1, &o2);
    out[0] = o0; out[1] = o1; out[2] = o2;
    if (t->cfg.track_inputs) {
        record(t, 0, training_canon(0, o1, o2));
        record(t, 1, training_canon(1, o1, o2));
    }
}

/* ---- cajas de golpe y de daño (docs/HITBOXES.md; formato de sfzch, D75) ------------------------------------------------------- */
static uint32_t rom8(const Mem *m, uint32_t a) { return a < m->rom_size ? m->rom[a] : 0; }
static uint32_t rom16(const Mem *m, uint32_t a) { return (rom8(m, a) << 8) | rom8(m, a + 1); }
/* las tablas de cajas están en la ROM o, en sfzch, también en la RAM de trabajo (proyectiles que cambian de tamaño) */
static uint32_t box8(const Mem *m, uint32_t a) { return a >= 0xFF0000u ? rd8(m, a) : rom8(m, a); }
static uint32_t box16(const Mem *m, uint32_t a) { return (box8(m, a) << 8) | box8(m, a + 1); }
static uint32_t ram32(const Mem *m, uint32_t a) { return ((uint32_t)rd16(m, a) << 16) | rd16(m, a + 2); }
static int box_ptr(const Mem *m, uint32_t a, uint32_t len) { return (a >= 0x400u && a + len <= m->rom_size) || a >= 0xFF0000u; }

static int add_box(const Mem *m, Sf2Box *out, int n, int max, uint32_t rec, uint8_t kind, uint8_t owner, int x, int y, int mirror) {
    if (n >= max || !box_ptr(m, rec, 4))
        return n;
    const int xo = (int8_t)box8(m, rec), yo = (int8_t)box8(m, rec + 1), rx = (int)box8(m, rec + 2), ry = (int)box8(m, rec + 3);
    const int cx = x + (mirror ? -xo : xo), cy = y + yo;
    const int cam_x = (int16_t)rd16(m, TR(m)->cam_x), cam_y = (int16_t)rd16(m, TR(m)->cam_y);
    const int sx = cx - cam_x, sy = 240 - (cy - cam_y);
    out[n].x = (int16_t)(sx - rx);
    out[n].y = (int16_t)(sy - ry);
    out[n].w = (int16_t)(2 * rx);
    out[n].h = (int16_t)(2 * ry);
    out[n].kind = kind;
    out[n].owner = owner;
    return n + 1;
}

/* SF2: un objeto (luchador o proyectil); `obj` es su dirección de RAM (la estructura de jugador + 6 en los luchadores) */
static int sf2_object_boxes(const Mem *m, Sf2Box *out, int n, int max, uint32_t obj, uint8_t owner, int proj) {
    if (!rd8(m, obj))
        return n;
    const int x = (int16_t)rd16(m, obj + 6), y = (int16_t)rd16(m, obj + 0xA), mirror = rd8(m, obj + 0x12) != 0;
    const uint32_t pose = ram32(m, obj + 0x1A), table = ram32(m, obj + 0x34);
    if (pose < 0x400u || pose + 0x18 > m->rom_size || table < 0x400u || table + 0xC > m->rom_size)
        return n;
    static const uint8_t kinds[6] = { SF2_BOX_HEAD, SF2_BOX_BODY, SF2_BOX_FOOT, SF2_BOX_WEAK, SF2_BOX_ATTACK, SF2_BOX_PUSH };
    for (int c = 0; c < 6; c++) {
        const uint32_t idx = rom8(m, pose + 8 + (uint32_t)c);
        if (!idx)
            continue;                                   /* índice 0 = sin caja */
        const uint32_t rec = table + rom16(m, table + 2u * (uint32_t)c) + idx * (c == 4 ? 12u : 4u);
        uint8_t kind = kinds[c];
        if (proj && c == 4) kind = SF2_BOX_PROJ_ATTACK;
        else if (proj && c == 5) kind = SF2_BOX_PROJ_PUSH;
        else if (proj) continue;                        /* los proyectiles solo dibujan ataque y empuje */
        n = add_box(m, out, n, max, rec, kind, owner, x, y, mirror);
    }
    return n;
}

/* sfzch (rutinas 0xDB0A, 0xDCC6, 0xE744, 0xE990 y 0xECC2): X en 0x10, Y en 0x14, espejo en 0xB y pose (puntero largo) en 0x20 del
 * objeto.  Luchador: los bytes 8, 9 y 0xA de la pose son las cajas vulnerables (tablas en 0x110, 0x114 y 0x118 del luchador,
 * registros de 4 bytes), 0xB el ataque (tabla en 0x11C, registros de 16) y 0xC el empuje (tabla en 0x120).  Proyectil (vivo con
 * 0x0 = 1): tabla en 0x50 con dos palabras de desplazamiento, empuje (byte 8 de la pose, registros de 4) y ataque (byte 0xB,
 * registros de 16).  Índice 0 = sin caja; el registro es el mismo centro + radios que en SF2. */
static int sfz_object_boxes(const Mem *m, Sf2Box *out, int n, int max, uint32_t obj, uint8_t owner, int proj) {
    if (proj ? rd8(m, obj) != 1 : !rd8(m, obj))
        return n;
    const int x = (int16_t)rd16(m, obj + 0x10), y = (int16_t)rd16(m, obj + 0x14), mirror = rd8(m, obj + 0xB) != 0;
    const uint32_t pose = ram32(m, obj + 0x20);
    if (pose < 0x400u || pose + 0x10 > m->rom_size)
        return n;
    if (proj) {
        const uint32_t table = ram32(m, obj + 0x50);
        if (!box_ptr(m, table, 4))
            return n;
        uint32_t idx = rom8(m, pose + 0xB);
        if (idx)
            n = add_box(m, out, n, max, table + box16(m, table + 2) + idx * 16u, SF2_BOX_PROJ_ATTACK, owner, x, y, mirror);
        idx = rom8(m, pose + 8);
        if (idx)
            n = add_box(m, out, n, max, table + box16(m, table) + idx * 4u, SF2_BOX_PROJ_PUSH, owner, x, y, mirror);
        return n;
    }
    static const uint8_t kinds[5] = { SF2_BOX_HEAD, SF2_BOX_BODY, SF2_BOX_FOOT, SF2_BOX_ATTACK, SF2_BOX_PUSH };
    for (uint32_t c = 0; c < 5; c++) {
        const uint32_t idx = rom8(m, pose + 8 + c);
        if (idx)
            n = add_box(m, out, n, max, ram32(m, obj + 0x110 + 4 * c) + idx * (c == 3 ? 16u : 4u), kinds[c], owner, x, y, mirror);
    }
    return n;
}

int training_boxes(const Mem *m, Sf2Box *out, int max) {
    const TrainingRam *r = TR(m);
    if (!r || r->box_fmt == BOX_NONE || rd8(m, r->mode) != r->mode_game || rd8(m, r->phase) != r->phase_fight)
        return 0;
    const int sfz = r->box_fmt == BOX_SFZ;
    const uint32_t off = sfz ? 0 : 6;                   /* SF2: el objeto es la estructura de jugador + 6 */
    int (*boxes)(const Mem *, Sf2Box *, int, int, uint32_t, uint8_t, int) = sfz ? sfz_object_boxes : sf2_object_boxes;
    int n = boxes(m, out, 0, max, r->p1 + off, 0, 0);
    n = boxes(m, out, n, max, r->p2 + off, 1, 0);
    for (int i = 0; i < r->proj_count; i++)
        n = boxes(m, out, n, max, r->proj + (uint32_t)i * r->proj_size, 2, 1);
    return n;
}
