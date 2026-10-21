/* input.c - teclado y mandos de SDL3 hacia las entradas de la CPS1 (ver input.h). */
#include "input.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sf2.h"

#define STICK_DEADZONE 12000
#define TRIGGER_ON     16000

typedef struct PadBind { int button; int axis; } PadBind;    /* button >= 0: botón; axis >= 0: gatillo; ambos -1: sin usar */

struct Input {
    SDL_Scancode keys[SF2_ACT_COUNT][INPUT_MAX_BINDS];
    PadBind pads[SF2_ACT_COUNT][INPUT_MAX_BINDS];
    SDL_Scancode hotkeys[SF2_HK_COUNT][INPUT_MAX_BINDS];
    SDL_Gamepad *pad[INPUT_MAX_PADS];
    SDL_JoystickID pad_id[INPUT_MAX_PADS];
};

/* separa "Up, W" en trozos sin espacios sobrantes; devuelve cuántos */
static int split_list(const char *text, char parts[][48], int max) {
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != ',') e++;
        const char *t = e;
        while (t > p && isspace((unsigned char)t[-1])) t--;
        size_t len = (size_t)(t - p);
        if (len >= 48) len = 47;
        memcpy(parts[n], p, len);
        parts[n][len] = 0;
        n++;
        p = e;
    }
    return n;
}

static void report(char *msg, size_t n, const char *fmt, const char *name, const char *what) {
    if (msg && n) {
        size_t len = strlen(msg);
        snprintf(msg + len, n - len, fmt, name, what);
    } else {
        fprintf(stderr, fmt, name, what);
    }
}

/* devuelve cuántos nombres no existen; los describe en `msg` (o por stderr si es NULL) */
static int resolve_keys(SDL_Scancode *dst, const char *text, const char *what, char *msg, size_t msgn) {
    char parts[INPUT_MAX_BINDS + 1][48];
    int n = split_list(text, parts, INPUT_MAX_BINDS), bad = 0;
    for (int i = 0; i < INPUT_MAX_BINDS; i++) dst[i] = SDL_SCANCODE_UNKNOWN;
    for (int i = 0; i < n; i++) {
        SDL_Scancode sc = SDL_GetScancodeFromName(parts[i]);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            report(msg, msgn, "configuracion: tecla desconocida \"%s\" en %s\n", parts[i], what);
            bad++;
        }
        dst[i] = sc;
    }
    return bad;
}

static int resolve_pad(PadBind *dst, const char *text, const char *what, char *msg, size_t msgn) {
    char parts[INPUT_MAX_BINDS + 1][48];
    int n = split_list(text, parts, INPUT_MAX_BINDS), bad = 0;
    for (int i = 0; i < INPUT_MAX_BINDS; i++) dst[i] = (PadBind){ -1, -1 };
    for (int i = 0; i < n; i++) {
        if (!SDL_strcasecmp(parts[i], "lefttrigger")) { dst[i].axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER; continue; }
        if (!SDL_strcasecmp(parts[i], "righttrigger")) { dst[i].axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; continue; }
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(parts[i]);
        if (b == SDL_GAMEPAD_BUTTON_INVALID) {
            report(msg, msgn, "configuracion: boton de mando desconocido \"%s\" en %s\n", parts[i], what);
            bad++;
        }
        dst[i].button = (int)b;
    }
    return bad;
}

static int resolve_all(Input *in, const Sf2Config *cfg, char *msg, size_t msgn) {
    int bad = 0;
    for (int a = 0; a < SF2_ACT_COUNT; a++) {
        char what[64];
        snprintf(what, sizeof what, "[keys] %s", config_action_name(a));
        bad += resolve_keys(in->keys[a], cfg->keys[a], what, msg, msgn);
        snprintf(what, sizeof what, "[pad] %s", config_action_name(a));
        bad += resolve_pad(in->pads[a], cfg->pads[a], what, msg, msgn);
    }
    for (int h = 0; h < SF2_HK_COUNT; h++) {
        char what[64];
        snprintf(what, sizeof what, "[hotkeys] %s", config_hotkey_name(h));
        bad += resolve_keys(in->hotkeys[h], cfg->hotkeys[h], what, msg, msgn);
    }
    return bad;
}

Input *input_create(const Sf2Config *cfg) {
    Input *in = calloc(1, sizeof *in);
    if (!in) return NULL;
    resolve_all(in, cfg, NULL, 0);
    return in;
}

int input_check_config(const Sf2Config *cfg, char *msg, size_t msgn) {
    Input *in = calloc(1, sizeof *in);
    if (!in) return -1;
    if (msg && msgn) msg[0] = 0;
    int bad = resolve_all(in, cfg, msg, msgn);
    free(in);
    return bad;
}

void input_reload(Input *in, const Sf2Config *cfg) { resolve_all(in, cfg, NULL, 0); }

void input_destroy(Input *in) {
    if (!in) return;
    for (int i = 0; i < INPUT_MAX_PADS; i++)
        if (in->pad[i]) SDL_CloseGamepad(in->pad[i]);
    free(in);
}

static void open_pad(Input *in, SDL_JoystickID id) {
    for (int i = 0; i < INPUT_MAX_PADS; i++)
        if (in->pad[i] && in->pad_id[i] == id) return;
    for (int i = 0; i < INPUT_MAX_PADS; i++) {
        if (in->pad[i]) continue;
        SDL_Gamepad *g = SDL_OpenGamepad(id);
        if (!g) { fprintf(stderr, "mando: %s\n", SDL_GetError()); return; }
        in->pad[i] = g;
        in->pad_id[i] = id;
        fprintf(stderr, "mando %d (jugador %d): %s\n", i + 1, i + 1, SDL_GetGamepadName(g));
        return;
    }
}

void input_open_existing(Input *in) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) open_pad(in, ids[i]);
    SDL_free(ids);
}

void input_handle_event(Input *in, const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
        open_pad(in, ev->gdevice.which);
    } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int i = 0; i < INPUT_MAX_PADS; i++)
            if (in->pad[i] && in->pad_id[i] == ev->gdevice.which) {
                SDL_CloseGamepad(in->pad[i]);
                in->pad[i] = NULL;
                fprintf(stderr, "mando %d desconectado\n", i + 1);
            }
    }
}

int input_pad_count(const Input *in) {
    int n = 0;
    for (int i = 0; i < INPUT_MAX_PADS; i++) n += in->pad[i] != NULL;
    return n;
}

static int key_pressed(const bool *kb, const SDL_Scancode *list) {
    for (int i = 0; i < INPUT_MAX_BINDS; i++)
        if (list[i] != SDL_SCANCODE_UNKNOWN && kb[list[i]]) return 1;
    return 0;
}

static int pad_pressed(SDL_Gamepad *g, const PadBind *list) {
    if (!g) return 0;
    for (int i = 0; i < INPUT_MAX_BINDS; i++) {
        if (list[i].button >= 0 && SDL_GetGamepadButton(g, (SDL_GamepadButton)list[i].button)) return 1;
        if (list[i].axis >= 0 && SDL_GetGamepadAxis(g, (SDL_GamepadAxis)list[i].axis) > TRIGGER_ON) return 1;
    }
    return 0;
}

/* el stick izquierdo suma a la cruceta */
static int stick(SDL_Gamepad *g, int dir /* 0 arriba, 1 abajo, 2 izquierda, 3 derecha */) {
    if (!g) return 0;
    int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    switch (dir) {
    case 0: return y < -STICK_DEADZONE;
    case 1: return y > STICK_DEADZONE;
    case 2: return x < -STICK_DEADZONE;
    default: return x > STICK_DEADZONE;
    }
}

int input_hotkey_matches(const Input *in, int hotkey, SDL_Scancode sc) {
    for (int i = 0; i < INPUT_MAX_BINDS; i++)
        if (in->hotkeys[hotkey][i] == sc && sc != SDL_SCANCODE_UNKNOWN) return 1;
    return 0;
}

int input_hotkey_down(const Input *in, int hotkey) {
    return key_pressed(SDL_GetKeyboardState(NULL), in->hotkeys[hotkey]);
}

void input_poll(Input *in, uint16_t *in0, uint16_t *in1, uint16_t *in2) {
    const bool *kb = SDL_GetKeyboardState(NULL);
    uint16_t r0 = 0, r1 = 0, r2 = 0;
#define PRESSED(act, player) (key_pressed(kb, in->keys[act]) || pad_pressed(in->pad[player], in->pads[act]))
    if (PRESSED(SF2_ACT_COIN1, 0)) r0 |= SF2_IN0_COIN1;
    if (PRESSED(SF2_ACT_START1, 0)) r0 |= SF2_IN0_START1;
    if (PRESSED(SF2_ACT_COIN2, 1)) r0 |= SF2_IN0_COIN2;
    if (PRESSED(SF2_ACT_START2, 1)) r0 |= SF2_IN0_START2;
    if (key_pressed(kb, in->keys[SF2_ACT_SERVICE]) || pad_pressed(in->pad[0], in->pads[SF2_ACT_SERVICE])) r0 |= SF2_IN0_SERVICE;
    for (int p = 0; p < 2; p++) {
        int base = p == 0 ? SF2_ACT_P1_UP : SF2_ACT_P2_UP;
        SDL_Gamepad *g = in->pad[p];
        unsigned shift = p == 0 ? 0 : 8;
        if (PRESSED(base + 3, p) || stick(g, 3)) r1 |= SF2_IN1_RIGHT << shift;
        if (PRESSED(base + 2, p) || stick(g, 2)) r1 |= SF2_IN1_LEFT << shift;
        if (PRESSED(base + 1, p) || stick(g, 1)) r1 |= SF2_IN1_DOWN << shift;
        if (PRESSED(base + 0, p) || stick(g, 0)) r1 |= SF2_IN1_UP << shift;
        if (PRESSED(base + 4, p)) r1 |= SF2_IN1_LP << shift;
        if (PRESSED(base + 5, p)) r1 |= SF2_IN1_MP << shift;
        if (PRESSED(base + 6, p)) r1 |= SF2_IN1_HP << shift;
        unsigned k = p == 0 ? 0 : 4;
        if (PRESSED(base + 7, p)) r2 |= SF2_IN2_LK << k;
        if (PRESSED(base + 8, p)) r2 |= SF2_IN2_MK << k;
        if (PRESSED(base + 9, p)) r2 |= SF2_IN2_HK << k;
    }
#undef PRESSED
    *in0 = r0;
    *in1 = r1;
    *in2 = r2;
}
