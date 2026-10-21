/*
 * input_test.c - el remapeo de teclado y mando (app/input.c) con un mando virtual de SDL3: los nombres por omision
 * existen, el mando 1 maneja al jugador 1 y el 2 al jugador 2, la cruceta, el stick y los gatillos funcionan y una
 * asignacion cambiada en la configuracion se obedece.  No necesita ROMs ni ventana (controlador de video "dummy").
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "input.h"
#include "sf2.h"

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static SDL_Joystick *attach(Input *in, const char *name) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = name;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (!id) { fprintf(stderr, "no se pudo crear el mando virtual: %s\n", SDL_GetError()); return NULL; }
    SDL_Event ev;
    SDL_PumpEvents();
    while (SDL_PollEvent(&ev)) input_handle_event(in, &ev);
    return SDL_OpenJoystick(id);
}

static void poll(Input *in, uint16_t *i0, uint16_t *i1, uint16_t *i2) {
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    input_poll(in, i0, i1, i2);
}

int main(void) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    /* solo los mandos virtuales de la prueba: un mando real conectado al equipo (hito 6: un Xbox One S por
     * Bluetooth) ocupaba el puesto del jugador 1 y la prueba fallaba sin que el código tuviera la culpa */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_IOKIT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 2; }
    Sf2Config cfg;
    config_defaults(&cfg);
    char msg[512];
    CHECK(input_check_config(&cfg, msg, sizeof msg) == 0);       /* todos los nombres por omision existen en SDL */
    if (msg[0]) fputs(msg, stderr);
    /* un nombre inventado se detecta */
    Sf2Config bad = cfg;
    strcpy(bad.keys[SF2_ACT_P1_LP], "Tecla Inventada");
    strcpy(bad.pads[SF2_ACT_P1_LK], "boton_raro");
    CHECK(input_check_config(&bad, msg, sizeof msg) == 2);
    CHECK(strstr(msg, "Tecla Inventada") != NULL && strstr(msg, "boton_raro") != NULL);

    Input *in = input_create(&cfg);
    uint16_t i0, i1, i2;
    poll(in, &i0, &i1, &i2);
    CHECK(i0 == 0 && i1 == 0 && i2 == 0);
    SDL_Joystick *p1 = attach(in, "mando 1");
    CHECK(p1 != NULL && input_pad_count(in) == 1);
    if (p1) {
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_WEST, true);          /* x: puñetazo débil */
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_SOUTH, true);         /* a: patada débil */
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_START, true);
        poll(in, &i0, &i1, &i2);
        CHECK(i1 == (SF2_IN1_LP | SF2_IN1_LEFT) && i2 == SF2_IN2_LK && i0 == SF2_IN0_START1);
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_WEST, false);
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_SOUTH, false);
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_DPAD_LEFT, false);
        SDL_SetJoystickVirtualButton(p1, SDL_GAMEPAD_BUTTON_START, false);
        /* stick izquierdo y gatillo derecho (patada fuerte) */
        SDL_SetJoystickVirtualAxis(p1, SDL_GAMEPAD_AXIS_LEFTY, -30000);
        SDL_SetJoystickVirtualAxis(p1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        poll(in, &i0, &i1, &i2);
        CHECK(i1 == SF2_IN1_UP && i2 == SF2_IN2_HK && i0 == 0);
        SDL_SetJoystickVirtualAxis(p1, SDL_GAMEPAD_AXIS_LEFTY, 0);
        SDL_SetJoystickVirtualAxis(p1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);     /* reposo de un gatillo: el minimo del eje */
        SDL_Joystick *p2 = attach(in, "mando 2");
        CHECK(p2 != NULL && input_pad_count(in) == 2);
        if (p2) {
            SDL_SetJoystickVirtualButton(p2, SDL_GAMEPAD_BUTTON_NORTH, true);      /* y: puñetazo medio del jugador 2 */
            SDL_SetJoystickVirtualButton(p2, SDL_GAMEPAD_BUTTON_EAST, true);       /* b: patada media del jugador 2 */
            SDL_SetJoystickVirtualButton(p2, SDL_GAMEPAD_BUTTON_START, true);
            poll(in, &i0, &i1, &i2);
            CHECK(i1 == (SF2_IN1_MP << 8) && i2 == (SF2_IN2_MK << 4) && i0 == SF2_IN0_START2);
            SDL_CloseJoystick(p2);
        }
        SDL_CloseJoystick(p1);
    }
    input_destroy(in);
    /* una asignacion cambiada en la configuracion se obedece: el boton b del mando pasa a ser el puñetazo fuerte */
    strcpy(cfg.pads[SF2_ACT_P1_HP], "b");
    strcpy(cfg.pads[SF2_ACT_P1_MK], "");
    in = input_create(&cfg);
    SDL_Joystick *q = attach(in, "mando remapeado");
    if (q) {
        SDL_SetJoystickVirtualButton(q, SDL_GAMEPAD_BUTTON_EAST, true);
        poll(in, &i0, &i1, &i2);
        CHECK(i1 == SF2_IN1_HP && i2 == 0);
        SDL_CloseJoystick(q);
    }
    input_destroy(in);
    SDL_Quit();
    printf("sf2_input_test: %d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
