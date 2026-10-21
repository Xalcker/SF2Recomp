/* config.c - configuración del juego (ver config.h). */
#include "config.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const action_names[SF2_ACT_COUNT] = {
    "coin1", "start1", "coin2", "start2", "service",
    "p1_up", "p1_down", "p1_left", "p1_right", "p1_lp", "p1_mp", "p1_hp", "p1_lk", "p1_mk", "p1_hk",
    "p2_up", "p2_down", "p2_left", "p2_right", "p2_lp", "p2_mp", "p2_hp", "p2_lk", "p2_mk", "p2_hk",
};
static const char *const hotkey_names[SF2_HK_COUNT] = {
    "save_state", "load_state", "slot_prev", "slot_next", "pause", "fast_forward", "fullscreen", "screenshot", "reset", "quit",
    "widescreen", "wide_logic", "stage_full", "hfr", "menu", "training", "show_inputs", "show_hitboxes",
};
/* precio por moneda: 4 monedas por crédito ... 1 moneda por 6 créditos; el valor de los dipswitches está en dsw_coin */
const char *const config_coinage_names[8] = { "4c1c", "3c1c", "2c1c", "1c1c", "1c2c", "1c3c", "1c4c", "1c6c" };
static const uint8_t dsw_coin[8] = { 0x00, 0x01, 0x02, 0x07, 0x06, 0x05, 0x04, 0x03 };    /* de CPS1_COINAGE_1 de MAME */

const char *const config_dummy_names[SF2_CONFIG_DUMMY_MODES] = { "stand", "crouch", "jump", "block", "block_crouch", "walk_forward", "walk_back", "script" };

const char *config_action_name(int a) { return a >= 0 && a < SF2_ACT_COUNT ? action_names[a] : "?"; }
const char *config_hotkey_name(int h) { return h >= 0 && h < SF2_HK_COUNT ? hotkey_names[h] : "?"; }

static void bind(char *dst, const char *s) { snprintf(dst, SF2_BIND_LEN, "%s", s); }

void config_defaults(Sf2Config *c) {
    memset(c, 0, sizeof *c);
    c->scale = 3;
    c->integer_scale = 1;
    c->vsync = 1;
    c->wide_width = 512;
    c->audio = 1;
    c->volume = 80;
    c->audio_rate = 48000;
    c->difficulty = 3;
    c->coin_a = c->coin_b = 3;
    c->free_play = 0;
    c->two_coins_to_start = 0;
    c->demo_sounds = 1;
    c->allow_continue = 1;
    c->freeze = 0;
    c->test_mode = 0;
    c->tr_infinite_time = 1;
    c->tr_refill = 1;
    c->tr_refill_delay = 60;
    c->tr_lock_p2 = 1;
    c->tr_auto_vs = 1;
    snprintf(c->tr_script, sizeof c->tr_script, "d:30 df:3 f+lp:2 n:30");
    static const char *const kb[SF2_ACT_COUNT] = {
        "5", "1", "6", "2", "F2",
        "Up", "Down", "Left", "Right", "Z", "X", "C", "A", "S", "D",
        "I", "K", "J", "L", "R", "T", "Y", "F", "G", "H",
    };
    static const char *const pad[SF2_ACT_COUNT] = {
        "back", "start", "back", "start", "",
        "dpup", "dpdown", "dpleft", "dpright", "x", "y", "rightshoulder", "a", "b", "righttrigger",
        "dpup", "dpdown", "dpleft", "dpright", "x", "y", "rightshoulder", "a", "b", "righttrigger",
    };
    for (int i = 0; i < SF2_ACT_COUNT; i++) {
        bind(c->keys[i], kb[i]);
        bind(c->pads[i], pad[i]);
    }
    static const char *const hk[SF2_HK_COUNT] = { "F5", "F7", "F6", "F8", "P", "Tab", "F11", "F12", "F10", "Escape", "F9", "F3", "F4", "F1", "M", "7", "8", "9" };
    for (int i = 0; i < SF2_HK_COUNT; i++)
        bind(c->hotkeys[i], hk[i]);
}

/* ---- dipswitches -------------------------------------------------------------------------------------------- */
void config_dsw(const Sf2Config *c, uint8_t *a, uint8_t *b, uint8_t *dswc) {
    /* DSWA: precio A (bits 0-2), precio B (3-5), "2 monedas para empezar, 1 para continuar" (6, activo a 0), 7 sin uso */
    *a = (uint8_t)(dsw_coin[c->coin_a & 7] | (dsw_coin[c->coin_b & 7] << 3) | (c->two_coins_to_start ? 0x00 : 0x40) | 0x80);
    /* DSWB: dificultad (bits 0-2, 7 = la más fácil), el resto sin uso */
    *b = (uint8_t)((7 - (c->difficulty & 7)) | 0xf8);
    /* DSWC: 2 y 1 sin uso; free play (bit 2), freeze (3), flip (4, siempre apagado), demo sounds (5), continuar (6), juego/test (7) */
    *dswc = (uint8_t)(0x03 | (c->free_play ? 0x00 : 0x04) | (c->freeze ? 0x00 : 0x08) | 0x10 | (c->demo_sounds ? 0x00 : 0x20) |
                      (c->allow_continue ? 0x00 : 0x40) | (c->test_mode ? 0x00 : 0x80));
}

/* ---- lectura ------------------------------------------------------------------------------------------------ */
typedef struct Warn { char *buf; size_t cap, len; int count; } Warn;

static void warn(Warn *w, int lineno, const char *fmt, ...) {
    w->count++;
    if (!w->buf || w->len + 1 >= w->cap)
        return;
    va_list ap;
    int n = snprintf(w->buf + w->len, w->cap - w->len, "linea %d: ", lineno);
    if (n > 0) w->len += (size_t)n;
    va_start(ap, fmt);
    n = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n > 0) w->len += (size_t)n;
    if (w->len + 2 < w->cap) { w->buf[w->len++] = '\n'; w->buf[w->len] = 0; }
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int parse_bool(const char *v, int *out) {
    if (ieq(v, "1") || ieq(v, "true") || ieq(v, "yes") || ieq(v, "on") || ieq(v, "si")) { *out = 1; return 0; }
    if (ieq(v, "0") || ieq(v, "false") || ieq(v, "no") || ieq(v, "off")) { *out = 0; return 0; }
    return -1;
}

static int parse_int(const char *v, int lo, int hi, int *out) {
    char *end;
    long x = strtol(v, &end, 10);
    if (end == v || *end || x < lo || x > hi)
        return -1;
    *out = (int)x;
    return 0;
}

static void set_int(Warn *w, int line, const char *key, const char *v, int lo, int hi, int *dst) {
    if (parse_int(v, lo, hi, dst) != 0)
        warn(w, line, "%s: se esperaba un entero entre %d y %d", key, lo, hi);
}

static void set_bool(Warn *w, int line, const char *key, const char *v, int *dst) {
    if (parse_bool(v, dst) != 0)
        warn(w, line, "%s: se esperaba true o false", key);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int config_parse(Sf2Config *c, const char *text, char *warnbuf, size_t warn_size) {
    Warn w = { warnbuf, warn_size, 0, 0 };
    if (warnbuf && warn_size) warnbuf[0] = 0;
    char section[24] = "";
    int lineno = 0;
    while (*text) {
        char line[512];
        size_t n = 0;
        while (text[n] && text[n] != '\n') n++;
        size_t k = n < sizeof line - 1 ? n : sizeof line - 1;
        memcpy(line, text, k);
        line[k] = 0;
        text += n;
        if (*text == '\n') text++;
        lineno++;
        char *p = trim(line);
        if (*p == '#' || *p == ';' || *p == 0) continue;
        if (*p == '[') {
            char *e = strchr(p, ']');
            if (!e) { warn(&w, lineno, "seccion sin cerrar"); continue; }
            *e = 0;
            snprintf(section, sizeof section, "%s", trim(p + 1));
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) { warn(&w, lineno, "se esperaba clave = valor"); continue; }
        *eq = 0;
        char *key = trim(p), *val = trim(eq + 1);
        if (ieq(section, "video")) {
            if (ieq(key, "scale")) set_int(&w, lineno, key, val, 0, 16, &c->scale);
            else if (ieq(key, "fullscreen")) set_bool(&w, lineno, key, val, &c->fullscreen);
            else if (ieq(key, "integer_scale")) set_bool(&w, lineno, key, val, &c->integer_scale);
            else if (ieq(key, "aspect")) {
                if (ieq(val, "pixel")) c->aspect43 = 0;
                else if (ieq(val, "4:3")) c->aspect43 = 1;
                else warn(&w, lineno, "aspect: pixel o 4:3");
            }
            else if (ieq(key, "smooth")) set_bool(&w, lineno, key, val, &c->smooth);
            else if (ieq(key, "vsync")) set_bool(&w, lineno, key, val, &c->vsync);
            else if (ieq(key, "widescreen")) set_bool(&w, lineno, key, val, &c->widescreen);
            else if (ieq(key, "wide_logic")) set_bool(&w, lineno, key, val, &c->wide_logic);
            else if (ieq(key, "stage_full")) set_bool(&w, lineno, key, val, &c->stage_full);
            else if (ieq(key, "hfr")) set_bool(&w, lineno, key, val, &c->hfr);
            else if (ieq(key, "hud")) {
                if (ieq(val, "center")) c->hud_edges = 0;
                else if (ieq(val, "edges")) c->hud_edges = 1;
                else warn(&w, lineno, "hud: center o edges");
            }
            else if (ieq(key, "wide_width")) {
                int x = c->wide_width;
                set_int(&w, lineno, key, val, 384, 800, &x);
                if (x & 1) warn(&w, lineno, "wide_width: debe ser par");
                else c->wide_width = x;
            }
            else warn(&w, lineno, "clave desconocida en [video]: %s", key);
        } else if (ieq(section, "audio")) {
            if (ieq(key, "enabled")) set_bool(&w, lineno, key, val, &c->audio);
            else if (ieq(key, "volume")) set_int(&w, lineno, key, val, 0, 100, &c->volume);
            else if (ieq(key, "rate")) set_int(&w, lineno, key, val, 8000, 192000, &c->audio_rate);
            else warn(&w, lineno, "clave desconocida en [audio]: %s", key);
        } else if (ieq(section, "dip")) {
            if (ieq(key, "difficulty")) set_int(&w, lineno, key, val, 0, 7, &c->difficulty);
            else if (ieq(key, "coin_a") || ieq(key, "coin_b")) {
                int *dst = ieq(key, "coin_a") ? &c->coin_a : &c->coin_b, found = -1;
                for (int i = 0; i < 8; i++) if (ieq(val, config_coinage_names[i])) found = i;
                if (found < 0) warn(&w, lineno, "%s: 4c1c, 3c1c, 2c1c, 1c1c, 1c2c, 1c3c, 1c4c o 1c6c", key);
                else *dst = found;
            }
            else if (ieq(key, "free_play")) set_bool(&w, lineno, key, val, &c->free_play);
            else if (ieq(key, "two_coins_to_start")) set_bool(&w, lineno, key, val, &c->two_coins_to_start);
            else if (ieq(key, "demo_sounds")) set_bool(&w, lineno, key, val, &c->demo_sounds);
            else if (ieq(key, "allow_continue")) set_bool(&w, lineno, key, val, &c->allow_continue);
            else if (ieq(key, "freeze")) set_bool(&w, lineno, key, val, &c->freeze);
            else if (ieq(key, "test_mode")) set_bool(&w, lineno, key, val, &c->test_mode);
            else warn(&w, lineno, "clave desconocida en [dip]: %s", key);
        } else if (ieq(section, "training")) {
            if (ieq(key, "enabled")) set_bool(&w, lineno, key, val, &c->tr_enabled);
            else if (ieq(key, "infinite_time")) set_bool(&w, lineno, key, val, &c->tr_infinite_time);
            else if (ieq(key, "refill")) {
                if (ieq(val, "off")) c->tr_refill = 0;
                else if (ieq(val, "delay")) c->tr_refill = 1;
                else if (ieq(val, "always")) c->tr_refill = 2;
                else warn(&w, lineno, "refill: off, delay o always");
            }
            else if (ieq(key, "refill_delay")) set_int(&w, lineno, key, val, 1, 600, &c->tr_refill_delay);
            else if (ieq(key, "lock_p2")) set_bool(&w, lineno, key, val, &c->tr_lock_p2);
            else if (ieq(key, "auto_vs")) set_bool(&w, lineno, key, val, &c->tr_auto_vs);
            else if (ieq(key, "dummy")) {
                int found = -1;
                for (int i = 0; i < SF2_CONFIG_DUMMY_MODES; i++) if (ieq(val, config_dummy_names[i])) found = i;
                if (found < 0) warn(&w, lineno, "dummy: stand, crouch, jump, block, block_crouch, walk_forward, walk_back o script");
                else c->tr_dummy = found;
            }
            else if (ieq(key, "script")) snprintf(c->tr_script, sizeof c->tr_script, "%s", val);
            else if (ieq(key, "show_inputs")) set_bool(&w, lineno, key, val, &c->show_inputs);
            else if (ieq(key, "show_hitboxes")) set_bool(&w, lineno, key, val, &c->show_hitboxes);
            else warn(&w, lineno, "clave desconocida en [training]: %s", key);
        } else if (ieq(section, "paths")) {
            if (ieq(key, "rom")) snprintf(c->rom_path, sizeof c->rom_path, "%s", val);
            else if (ieq(key, "state_dir")) snprintf(c->state_dir, sizeof c->state_dir, "%s", val);
            else warn(&w, lineno, "clave desconocida en [paths]: %s", key);
        } else if (ieq(section, "keys") || ieq(section, "pad") || ieq(section, "hotkeys")) {
            int found = 0;
            if (ieq(section, "hotkeys")) {
                for (int i = 0; i < SF2_HK_COUNT; i++)
                    if (ieq(key, hotkey_names[i])) { bind(c->hotkeys[i], val); found = 1; }
            } else {
                for (int i = 0; i < SF2_ACT_COUNT; i++)
                    if (ieq(key, action_names[i])) { bind(ieq(section, "keys") ? c->keys[i] : c->pads[i], val); found = 1; }
            }
            if (!found) warn(&w, lineno, "accion desconocida en [%s]: %s", section, key);
        } else {
            warn(&w, lineno, "clave fuera de una seccion conocida ([video], [audio], [dip], [training], [paths], [keys], [pad], [hotkeys]): %s", key);
        }
    }
    return w.count;
}

/* ---- escritura ---------------------------------------------------------------------------------------------- */
typedef struct Out { char *buf; size_t cap, len; } Out;

static void put(Out *o, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = o->buf && o->len < o->cap ? vsnprintf(o->buf + o->len, o->cap - o->len, fmt, ap) : vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) o->len += (size_t)n;
}

static const char *yn(int v) { return v ? "true" : "false"; }

size_t config_format(const Sf2Config *c, char *out, size_t cap) {
    Out o = { out, cap, 0 };
    put(&o, "# SF2Recomp - configuracion\n"
            "# Edita este archivo con el juego cerrado.  Los valores no validos se ignoran con un aviso al arrancar.\n\n");
    put(&o, "[video]\n# scale: 0 = ajustar a la pantalla, N = ventana de (384*N) x (224*N)\nscale = %d\n", c->scale);
    put(&o, "# aspect: pixel = pixeles cuadrados (384x224); 4:3 = proporcion de la pantalla de la recreativa\naspect = %s\n", c->aspect43 ? "4:3" : "pixel");
    put(&o, "fullscreen = %s\n# integer_scale: escalar solo por factores enteros (pixeles sin deformar)\ninteger_scale = %s\n", yn(c->fullscreen), yn(c->integer_scale));
    put(&o, "# smooth: false = pixeles nitidos, true = filtro lineal\nsmooth = %s\nvsync = %s\n", yn(c->smooth), yn(c->vsync));
    put(&o, "# hfr: dibuja cuadros intermedios (camara y sprites interpolados) a cada refresco del monitor; el juego sigue a 59,6 Hz y no cambia.\n"
            "# Pide vsync y un monitor de mas de 60 Hz para notarse; anade un cuadro de latencia.  Tecla: hfr en [hotkeys]\nhfr = %s\n", yn(c->hfr));
    put(&o, "# widescreen: muestra mas escenario a los lados en los combates (las demas pantallas se centran con barras negras);\n"
            "# la pantalla de la recreativa no se estira.  wide_width: ancho del cuadro, par, de 384 a 800; con aspect = 4:3\n"
            "# 512 da una ventana 16:9.  Tecla: widescreen en [hotkeys]\nwidescreen = %s\nwide_width = %d\n"
            "# hud: center = el marcador del combate donde lo compone el juego; edges = pegado a los bordes de la ventana ancha\nhud = %s\n"
            "# wide_logic: con widescreen, los luchadores se mueven por todo el ancho de la ventana ancha y la camara se queda dentro del arte.\n"
            "# CAMBIA EL JUEGO (ya no es identico a la recreativa); false = juego original.  Tecla: wide_logic en [hotkeys]\nwide_logic = %s\n"
            "# stage_full: con widescreen, camara fija en el centro y todo el escenario a la vista (768 de ancho = el recorrido completo, para\n"
            "# monitores ultraanchos: 3440x1440 con aspect = 4:3 pide wide_width = 688; 768 deja franjas finas arriba y abajo).  CAMBIA EL JUEGO.\n"
            "stage_full = %s\n\n",
        yn(c->widescreen), c->wide_width, c->hud_edges ? "edges" : "center", yn(c->wide_logic), yn(c->stage_full));
    put(&o, "[audio]\nenabled = %s\n# volume: 0 a 100\nvolume = %d\n# rate: frecuencia del dispositivo de audio; la mezcla es de 48000 Hz y SDL la convierte\nrate = %d\n\n",
        yn(c->audio), c->volume, c->audio_rate);
    put(&o, "[dip]\n# dipswitches de la placa (los valores de MAME entre parentesis)\n"
            "# difficulty: 0 (mas facil) a 7 (mas dificil); 3 = normal\ndifficulty = %d\n"
            "# coin_a, coin_b: monedas por credito: 4c1c 3c1c 2c1c 1c1c 1c2c 1c3c 1c4c 1c6c\ncoin_a = %s\ncoin_b = %s\n"
            "free_play = %s\ntwo_coins_to_start = %s\ndemo_sounds = %s\nallow_continue = %s\nfreeze = %s\n"
            "# test_mode: arranca en el menu de pruebas de la placa\ntest_mode = %s\n\n",
        c->difficulty, config_coinage_names[c->coin_a & 7], config_coinage_names[c->coin_b & 7], yn(c->free_play),
        yn(c->two_coins_to_start), yn(c->demo_sounds), yn(c->allow_continue), yn(c->freeze), yn(c->test_mode));
    put(&o, "[training]\n# Mod de entrenamiento: CAMBIA EL JUEGO mientras enabled = true (false = juego original).  Tecla: training en [hotkeys].\n"
            "# Con auto_vs se mete monedas y se empieza una partida versus en el titulo; J1 elige luchador y J2 (el dummy) elige solo.\n"
            "enabled = %s\ninfinite_time = %s\n"
            "# refill: off, delay (barra llena tras refill_delay cuadros sin recibir dano) o always (nunca hay KO)\nrefill = %s\nrefill_delay = %d\n"
            "# lock_p2: J2 no responde al teclado ni al mando; lo maneja el dummy\nlock_p2 = %s\nauto_vs = %s\n"
            "# dummy: stand crouch jump block (se cubre de pie cuando J1 ataca) block_crouch walk_forward walk_back script\ndummy = %s\n"
            "# script: pasos \"botones:cuadros\" en bucle, separados por espacios; direcciones relativas al rival (f b u d df db uf ub; l r absolutas)\n"
            "# y botones lp mp hp lk mk hk; con + se pulsan a la vez; n = nada.  Ejemplo: \"d:30 df:3 f+lp:2 n:30\"\nscript = %s\n"
            "# visores (teclas show_inputs y show_hitboxes): entradas de J1 y J2, y cajas de golpe y de dano\nshow_inputs = %s\nshow_hitboxes = %s\n\n",
        yn(c->tr_enabled), yn(c->tr_infinite_time), c->tr_refill == 0 ? "off" : c->tr_refill == 1 ? "delay" : "always", c->tr_refill_delay,
        yn(c->tr_lock_p2), yn(c->tr_auto_vs), config_dummy_names[c->tr_dummy & 7], c->tr_script, yn(c->show_inputs), yn(c->show_hitboxes));
    put(&o, "[paths]\n# rom: sf2.zip de MAME o carpeta con las ROMs sueltas (vacio = buscar sf2.zip junto al juego)\nrom = %s\n"
            "# state_dir: carpeta de los estados guardados (vacio = la de preferencias)\nstate_dir = %s\n\n", c->rom_path, c->state_dir);
    put(&o, "[keys]\n# nombres de tecla de SDL (Up, Left Shift, Z, F5, Keypad 4...); varias separadas por comas\n");
    for (int i = 0; i < SF2_ACT_COUNT; i++) put(&o, "%s = %s\n", action_names[i], c->keys[i]);
    put(&o, "\n[pad]\n# botones del mando (nombres de SDL): a b x y back start leftshoulder rightshoulder leftstick rightstick\n"
            "# dpup dpdown dpleft dpright lefttrigger righttrigger.  El mando 1 es el jugador 1 y el 2 el jugador 2;\n"
            "# la cruceta y el stick izquierdo mueven siempre\n");
    for (int i = 0; i < SF2_ACT_COUNT; i++) put(&o, "%s = %s\n", action_names[i], c->pads[i]);
    put(&o, "\n[hotkeys]\n# save_state/load_state: guardar y cargar la ranura actual; slot_prev/slot_next: cambiar de ranura (0-9)\n");
    for (int i = 0; i < SF2_HK_COUNT; i++) put(&o, "%s = %s\n", hotkey_names[i], c->hotkeys[i]);
    if (out && cap) out[o.len < cap ? o.len : cap - 1] = 0;
    return o.len;
}
