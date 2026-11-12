/* sf2.c - fachada del núcleo (ver sf2.h). */
#include "sf2.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crc32.h"
#include "gfx.h"
#include "machine.h"
#include "recomp_funcs.h"
#include "sound.h"
#include "state.h"
#include "training_priv.h"
#include "video.h"

#define STATE_MAGIC   "SF2R"
#define STATE_VERSION 2u            /* 2: hito 6, instante de la IRQ2 (reloj de MAME, D47) */
#define STATE_HEADER  24u            /* marca, versión, CRC del código, longitud, CRC del contenido, reservado */
#define AUDIO_MAX     (SF2_AUDIO_RATE * 2u)     /* si el anfitrión no consume, se descarta lo más antiguo pasados 2 s (muestras por canal) */

struct Sf2 {
    Machine mc;                      /* primer miembro (ganchos y depuración) */
    Gfx gfx;
    Video video;
    Cps1Sound snd;
    int has_video, has_sound;
    const GameDef *game;             /* variante del código generado (recomp_game) */
    uint8_t *prog;                   /* imagen de programa (game->prog_size bytes; la máquina la lee, no la copia) */
    float *audio;
    size_t audio_len, audio_cap;     /* en floats: muestras por canales */
    unsigned audio_channels;         /* 1 = mono (el original), 2 = estéreo (mod de sonido estéreo) */
    uint8_t *scratch;
    size_t scratch_cap;
    int flip;
    int stage_full;                  /* stage completo (D59): cámara fija y todo el escenario a la vista */
    int dev_tool;                    /* herramienta de desarrollo pedida (sf2_set_dev_tool), 0 = juego normal */
    uint8_t user_dswc;               /* DSWC pedido por el anfitrión; el de la máquina vale esto salvo con una herramienta */
    int wide_logic;                  /* mod de lógica ancha pedido (D58): se aplica si el ancho del cuadro es mayor que 384 */
    Training train;                  /* mod de entrenamiento (training.h); apagado = juego original */
    uint16_t host_in[3];             /* entradas del anfitrión tal cual (sf2_set_input); las que ve el juego pasan por el mod */
    Sf2SampleHook sample_hook;
    void *sample_user;
    /* pantalla ancha con la lógica original (D62): máquina sombra que repite cada cuadro desde el estado real con las ventanas de
     * visibilidad ensanchadas; de ella solo se toma la tabla de sprites, así que la partida real no cambia */
    Machine shadow;
    int shadow_init, shadow_ready, shadow_failed, shadow_next_ok, shadow_vb;
    uint8_t *shadow_buf;
    size_t shadow_cap;
    uint16_t shadow_obj[OBJ_WORDS];       /* tabla para el VBlank de este cuadro */
    uint16_t shadow_next[OBJ_WORDS];      /* la que se acaba de calcular, para el VBlank del cuadro siguiente */
};

static void set_err(char *err, size_t n, const char *fmt, ...) SF2_PRINTF(3, 4);
static void set_err(char *err, size_t n, const char *fmt, ...) {
    if (!err || !n)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

const GameDef *sf2_game(void) { return recomp_game(); }

double sf2_frame_hz(void) {
    const GameDef *g = recomp_game();
    return game_frame_hz(g ? g : &game_sf2);
}

void sf2_options_default(Sf2Options *o) {
    o->video = 1;
    o->sound = 1;
    o->dswa = MEM_DSWA_DEFAULT;
    o->dswb = MEM_DSWB_DEFAULT;
    o->dswc = MEM_DSWC_DEFAULT;
}

static void apply_wide_logic(Sf2 *s);

static void on_vblank(Machine *mc) {
    Sf2 *s = mc->user;
    if (s->has_video) {
        video_vblank(&s->video, &mc->mem);
        if (s->shadow_ready) {           /* tabla de sprites de la máquina sombra (D62): la misma partida, con las ventanas ensanchadas */
            memcpy(s->video.obj, s->shadow_obj, sizeof s->video.obj);
            s->shadow_ready = 0;
        }
        apply_wide_logic(s);             /* la escena que se acaba de dibujar decide si el juego corre con los parches activos */
        if (s->video.flip_warned)
            s->flip = 1;
    }
    if (s->train.cfg.enabled || s->train.cfg.track_inputs) {
        /* mod de entrenamiento: toca la RAM y calcula las entradas que leerá la IRQ2 que se entrega a continuación */
        uint16_t out[3];
        training_vblank(&s->train, &mc->mem, s->host_in, out);
        mc->cps.manual_in0 = out[0];
        mc->cps.manual_in1 = out[1];
        mc->cps.manual_in2 = out[2];
    }
}

static void on_sample(Machine *mc, uint64_t frame) {
    Sf2 *s = mc->user;
    if (s->sample_hook)
        s->sample_hook(s->sample_user, frame, s->video.frame);
}

static void on_audio(void *user, const float *buf, unsigned frames, unsigned channels) {
    Sf2 *s = user;
    if (channels != s->audio_channels) {       /* el mod estéreo cambió entre dos bloques: lo pendiente ya no vale */
        s->audio_channels = channels;
        s->audio_len = 0;
    }
    const size_t n = (size_t)frames * channels;
    if (s->audio_len + n > AUDIO_MAX * channels)
        s->audio_len = 0;
    if (s->audio_len + n > s->audio_cap) {
        size_t cap = s->audio_cap ? s->audio_cap : 4096;
        while (cap < s->audio_len + n)
            cap *= 2;
        float *a = realloc(s->audio, cap * sizeof *a);
        if (!a)
            return;
        s->audio = a;
        s->audio_cap = cap;
    }
    memcpy(s->audio + s->audio_len, buf, n * sizeof *buf);
    s->audio_len += n;
}

void sf2_destroy(Sf2 *s) {
    if (!s)
        return;
    if (s->has_sound)
        sound_free(&s->snd);
    gfx_free(&s->gfx);
    machine_free(&s->mc);
    if (s->shadow_init) {
        s->shadow.cps.events = NULL;         /* el guion de entradas es el de la máquina real (ya liberado) */
        machine_free(&s->shadow);
    }
    free(s->shadow_buf);
    free(s->prog);
    free(s->audio);
    free(s->scratch);
    free(s);
}

int sf2_create(Sf2 **out, const RomSet *roms, const Sf2Options *opt, char *err, size_t errn) {
    *out = NULL;
    const GameDef *game = recomp_game();
    if (!game) {
        set_err(err, errn, "el codigo generado es de un juego desconocido (\"%s\")", recomp_game_id);
        return -1;
    }
    if (roms->game != game) {
        set_err(err, errn, "las ROMs son de %s (set \"%s\") y este ejecutable es de %s (set \"%s\")",
                roms->game ? roms->game->title : "?", roms->game ? roms->game->id : "?", game->title, game->id);
        return -1;
    }
    if (!game->program_known) {
        set_err(err, errn, "%s (set \"%s\") todavia no se puede jugar: falta analizar su programa (docs/VARIANTES.md)", game->title, game->id);
        return -1;
    }
    for (int i = 0; i < game->nroms; i++)
        if (!roms->data[i]) {
            set_err(err, errn, "faltan ROMs (%s)", game->roms[i].name);
            return -1;
        }
    Sf2Options def;
    if (!opt) {
        sf2_options_default(&def);
        opt = &def;
    }
    Sf2 *s = calloc(1, sizeof *s);
    if (!s) { set_err(err, errn, "sin memoria"); return -1; }
    s->game = game;
    s->prog = calloc(game->prog_size, 1);
    if (!s->prog) { set_err(err, errn, "sin memoria"); free(s); return -1; }
    romset_build_program(roms, s->prog);
    uint32_t crc = sf2_crc32(0, s->prog, game->prog_size);
    if (crc != recomp_rom_crc32) {
        set_err(err, errn, "las ROMs de programa no son las que se usaron al generar el codigo (CRC de la imagen %08x, "
                           "esperado %08x): regenera el ejecutable con estas ROMs (SF2_ROM_DIR)", crc, recomp_rom_crc32);
        free(s->prog);
        free(s);
        return -1;
    }
    s->has_video = opt->video != 0;
    s->has_sound = opt->sound != 0;
    if (s->has_video && gfx_init(&s->gfx, roms) < 0) {
        set_err(err, errn, "no se pudieron preparar los graficos");
        free(s->prog);
        free(s);
        return -1;
    }
    machine_init(&s->mc, game, s->prog, game->frame_cycles);
    s->mc.user = s;
    s->mc.quiet = 1;
    s->mc.on_sample = on_sample;
    video_init(&s->video, &s->gfx);
    s->mc.on_vblank = on_vblank;
    training_init(&s->train);
    if (s->has_sound) {
        if (sound_init(&s->snd, roms) < 0) {
            set_err(err, errn, "no se pudo preparar la placa de sonido");
            s->has_sound = 0;
            sf2_destroy(s);
            return -1;
        }
        s->snd.on_audio = on_audio;
        s->audio_channels = 1;
        s->snd.user = s;
        machine_attach_sound(&s->mc, &s->snd);
    }
    sf2_set_dips(s, opt->dswa, opt->dswb, opt->dswc);
    *out = s;
    return 0;
}

#define DEV_P2_LP_HP   0x5000        /* P2 puñetazo débil y fuerte: el juego lo mira al encender (0x994) */
#define DEV_P2_FRAMES  3u
#define DEV_TOOL_FRAMES 185u         /* el juego decide la herramienta hacia el cuadro 175 (medido); en 180 ya no hace falta el botón */

static void apply_dev_tool(Sf2 *s) {
    static const uint16_t buttons[3] = { 0, SF2_IN1_LP, SF2_IN1_MP };
    Cps1 *c = &s->mc.cps;
    memset(c->dev_in1, 0, sizeof c->dev_in1);
    if (s->dev_tool > 0 && s->dev_tool <= 2) {
        c->dev_in1[0] = DEV_P2_LP_HP;
        c->dev_until[0] = DEV_P2_FRAMES;
        c->dev_in1[1] = buttons[s->dev_tool];
        c->dev_until[1] = DEV_TOOL_FRAMES;
    }
    s->mc.mem.dswc = s->dev_tool ? (uint8_t)(s->user_dswc & 0x7f) : s->user_dswc;
}

void sf2_set_dev_tool(Sf2 *s, int tool) {
    s->dev_tool = tool < 0 || tool > 2 ? 0 : tool;
    sf2_reset(s);
}

int sf2_dev_tool(const Sf2 *s) { return s->dev_tool; }

void sf2_reset(Sf2 *s) {
    training_reset(&s->train);
    apply_dev_tool(s);
    machine_reset(&s->mc);
    s->shadow_next_ok = s->shadow_ready = 0;      /* la tabla de la máquina sombra era de la partida anterior */
    s->audio_len = 0;
    s->flip = 0;
    if (s->has_video)
        video_render(&s->video, &s->mc.mem);
}

void sf2_set_input(Sf2 *s, uint16_t in0, uint16_t in1, uint16_t in2) {
    s->host_in[0] = in0;
    s->host_in[1] = in1;
    s->host_in[2] = in2;
    training_inputs(&s->train, in0, in1, in2, &s->mc.cps.manual_in0, &s->mc.cps.manual_in1, &s->mc.cps.manual_in2);
}

int sf2_training_set(Sf2 *s, const Sf2Training *t, char *err, size_t errn) {
    int was = s->train.cfg.enabled || s->train.cfg.track_inputs;
    if (training_configure(&s->train, t, err, errn) != 0)
        return -1;
    if (!was && (s->train.cfg.enabled || s->train.cfg.track_inputs))
        training_reset(&s->train);
    sf2_set_input(s, s->host_in[0], s->host_in[1], s->host_in[2]);
    return 0;
}

void sf2_training_get(const Sf2 *s, Sf2Training *t) { *t = s->train.cfg; }
int sf2_training_history(const Sf2 *s, int player, Sf2InputEvent *out, int max) { return training_history(&s->train, player, out, max); }
int sf2_training_boxes(const Sf2 *s, Sf2Box *out, int max) { return training_boxes(&s->mc.mem, out, max); }
uint16_t sf2_training_pad(const Sf2 *s, int player) { return player >= 0 && player <= 1 ? s->train.now[player] : 0; }

int sf2_load_input_script(Sf2 *s, const char *text, const char *name, char *err, size_t errn) {
    int n = cps1_parse_inputs(&s->mc.cps, text, name, err, errn);
    if (n == -1 && err && errn)
        snprintf(err, errn, "sin memoria");
    cps1_apply_inputs(&s->mc.cps, s->mc.cps.frame);
    return n < 0 ? -1 : n;
}

void sf2_set_dips(Sf2 *s, uint8_t a, uint8_t b, uint8_t c) {
    s->mc.mem.dswa = a;
    s->mc.mem.dswb = b;
    s->user_dswc = c;
    s->mc.mem.dswc = s->dev_tool ? (uint8_t)(c & 0x7f) : c;
}

/* ---- máquina sombra de la pantalla ancha (D62) ------------------------------------------------------------------------------
 * Con la pantalla ancha y la lógica original, las ventanas «¿está a la vista?» del juego (pensadas para 384 px) hacen que los objetos de
 * las bandas añadidas aparezcan de golpe.  Ensancharlas en la propia partida la cambia (dibujar más objetos cuesta ciclos del 68000 y el
 * juego se ralentiza de otra forma: la X del rival divergía en el combate contra Dhalsim de ryu.txt), así que se hace en una copia: antes
 * de cada cuadro se copia la placa principal (sin sonido: el Z80 no influye en el 68000), la copia corre el cuadro con mod_view/mod_bx y
 * su tabla de sprites sustituye a la real al latchearla.  La copia no arrastra diferencias porque cada cuadro parte del estado real. */
static void shadow_vblank(Machine *mc) {
    Sf2 *s = mc->user;
    if (++s->shadow_vb < 2)
        return;                          /* el primer VBlank latchea la tabla que construyó la máquina real: solo vale el segundo */
    const uint32_t base = ((uint32_t)mc->mem.cps_a[CPSA_OBJ_BASE] * 256u) & ~(uint32_t)0x7ff & 0x3ffffu;
    for (int i = 0; i < OBJ_WORDS; i++) {
        const uint32_t off = base + 2u * (uint32_t)i;
        s->shadow_next[i] = off + 1 < GFXRAM_SIZE ? (uint16_t)(mc->mem.gfxram[off] << 8 | mc->mem.gfxram[off + 1]) : 0;
    }
    s->shadow_next_ok = 1;
}

static void shadow_frame(Sf2 *s) {
    /* lo calculado en el cuadro anterior es la tabla del VBlank de este */
    s->shadow_ready = s->shadow_next_ok;
    if (s->shadow_ready)
        memcpy(s->shadow_obj, s->shadow_next, sizeof s->shadow_obj);
    s->shadow_next_ok = 0;
    if (s->shadow_failed || !s->has_video || s->wide_logic || s->stage_full || s->video.w <= SF2_SCREEN_W || s->video.rw <= SF2_SCREEN_W)
        return;
    if (!s->shadow_init) {
        machine_init(&s->shadow, s->game, s->prog, s->game->frame_cycles);
        s->shadow.quiet = 1;
        s->shadow.user = s;
        s->shadow.on_vblank = shadow_vblank;
        s->shadow_init = 1;
    }
    StateStream st;
    state_begin_save(&st, NULL, 0);
    machine_state_ex(&s->mc, &st, 0);
    if (st.pos > s->shadow_cap) {
        uint8_t *b = realloc(s->shadow_buf, st.pos);
        if (!b) { s->shadow_failed = 1; return; }
        s->shadow_buf = b;
        s->shadow_cap = st.pos;
    }
    state_begin_save(&st, s->shadow_buf, s->shadow_cap);
    machine_state_ex(&s->mc, &st, 0);
    StateStream ld;
    state_begin_load(&ld, s->shadow_buf, st.pos);
    machine_state_ex(&s->shadow, &ld, 0);
    if (st.error || ld.error) { s->shadow_failed = 1; return; }
    /* entradas y configuración que no forman parte del estado */
    Cps1 *c = &s->shadow.cps, *r = &s->mc.cps;
    c->events = r->events;
    c->nevents = r->nevents;
    c->manual_in0 = r->manual_in0; c->manual_in1 = r->manual_in1; c->manual_in2 = r->manual_in2;
    memcpy(c->dev_in1, r->dev_in1, sizeof c->dev_in1);
    memcpy(c->dev_until, r->dev_until, sizeof c->dev_until);
    s->shadow.mem.in0 = s->mc.mem.in0; s->shadow.mem.in1 = s->mc.mem.in1; s->shadow.mem.in2 = s->mc.mem.in2;
    s->shadow.mem.dswa = s->mc.mem.dswa; s->shadow.mem.dswb = s->mc.mem.dswb; s->shadow.mem.dswc = s->mc.mem.dswc;
    M68K *cpu = &s->shadow.cpu;
    cpu->mod_wide = cpu->mod_fixcam = cpu->mod_stereo = 0;
    cpu->mod_view = (s->video.rw - SF2_SCREEN_W) / 2;
    cpu->mod_bx = s->video.rw > 512 ? (s->video.rw - 512) / 2 : 0;
    /* la tabla que se latchea en un VBlank se construye en el cuadro anterior: la copia corre dos cuadros y se queda con el segundo
     * latch, que la máquina real usará en su próximo VBlank */
    const uint64_t v0 = s->shadow.vblanks;
    s->shadow_vb = 0;
    for (int guard = 0; s->shadow.vblanks < v0 + 2 && guard < 128; guard++) {
        int r2 = machine_step(&s->shadow);
        if (r2 == MACHINE_ERROR) { s->shadow_failed = 1; s->shadow_next_ok = 0; return; }
        if (r2 == MACHINE_DONE) break;
    }
}

int sf2_run_frame(Sf2 *s) {
    shadow_frame(s);
    uint64_t v0 = s->mc.vblanks;
    do {
        int st = machine_step(&s->mc);
        if (st == MACHINE_ERROR)
            return -1;
        if (st == MACHINE_DONE)
            break;
    } while (s->mc.vblanks == v0);
    return (int)(s->mc.vblanks - v0);
}

const char *sf2_error(const Sf2 *s) { return s->mc.error; }

int sf2_dispatch_failure(const Sf2 *s, uint32_t *from, uint32_t *to) {
    if (s->mc.status != MACHINE_ERROR || s->mc.dispatch_misses == 0)
        return 0;
    if (from) *from = s->mc.miss_from;
    if (to) *to = s->mc.miss_to;
    return 1;
}
const uint32_t *sf2_frame(const Sf2 *s) { return s->video.frame; }
int sf2_frame_width(const Sf2 *s) { return s->video.w; }

void sf2_video_snapshot(const Sf2 *s, int which, uint16_t *obj, uint16_t *scroll) {
    const VideoSnap *st = which ? &s->video.cur : &s->video.prev;
    if (obj) memcpy(obj, st->obj, sizeof st->obj);
    for (int k = 0; scroll && k < 3; k++) { scroll[k] = st->scrollx[k]; scroll[3 + k] = st->scrolly[k]; }
}

void sf2_render_interp(Sf2 *s, int t256) {
    if (s->has_video)
        video_render_t(&s->video, t256);
}

void sf2_render_snapshot(Sf2 *s) {
    if (s->has_video)
        video_render_snap(&s->video, &s->video.cur);
}

void sf2_set_hud_edges(Sf2 *s, int on) {
    video_set_hud_edges(&s->video, on);
    if (s->has_video)
        video_render(&s->video, &s->mc.mem);
}

/* Lógica ancha y stage completo (D58, D59): los píxeles extra por lado de los parches del juego valen solo mientras la escena que
 * se está dibujando es de las que se ensanchan; en las demás (menús, selección) el juego corre sin tocar. */
static void apply_wide_logic(Sf2 *s) {
    const int on = (s->wide_logic || s->stage_full) && s->video.rw > SF2_SCREEN_W;
    s->mc.cpu.mod_wide = on ? (s->video.rw - SF2_SCREEN_W) / 2 : 0;
    s->mc.cpu.mod_fixcam = on && s->stage_full;
    /* dibujo (D62), con o sin lógica ancha: las ventanas «¿está a la vista?» se ensanchan lo que la vista, para que los objetos de las
     * bandas añadidas no aparezcan de golpe; el constructor de sprites admite X fuera de 0-511 solo si la vista pasa de 512 (hasta
     * 512 la X de 9 bits ya cubre la vista entera) y entonces el renderizador lee la X con signo */
    /* en la partida real solo con lógica ancha o stage completo (que ya cambian el juego); con la lógica original lo hace la máquina sombra */
    s->mc.cpu.mod_view = on ? (s->video.rw - SF2_SCREEN_W) / 2 : 0;
    s->mc.cpu.mod_bx = on && s->video.rw > 512 ? (s->video.rw - 512) / 2 : 0;
    video_set_xsigned(&s->video, s->video.w > 512);
    if (s->has_sound)
        sound_set_pan_half(&s->snd, s->video.rw / 2);     /* mod estéreo: el borde de la vista es el extremo del paneo */
}

void sf2_set_stereo(Sf2 *s, int on) {
    if (!s->has_sound)
        return;
    s->mc.cpu.mod_stereo = on != 0;
    sound_set_stereo(&s->snd, on);
}

int sf2_stereo(const Sf2 *s) { return s->has_sound && s->snd.stereo; }

void sf2_set_stereo_width(Sf2 *s, int percent) {
    if (s->has_sound)
        sound_set_pan_width(&s->snd, (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0f);
}

unsigned sf2_audio_channels(const Sf2 *s) { return s->audio_channels ? s->audio_channels : 1; }

void sf2_set_wide_logic(Sf2 *s, int on) {
    s->wide_logic = on != 0;
    apply_wide_logic(s);
}

void sf2_set_stage_full(Sf2 *s, int on) {
    s->stage_full = on != 0;
    apply_wide_logic(s);
}

int sf2_wide_logic_extra(const Sf2 *s) { return s->mc.cpu.mod_wide; }

int sf2_set_video_width(Sf2 *s, int width) {
    if (video_set_width(&s->video, width) != 0)
        return -1;
    apply_wide_logic(s);
    if (s->has_video)
        video_render(&s->video, &s->mc.mem);
    return 0;
}

const float *sf2_audio(const Sf2 *s, size_t *count) {
    *count = s->audio_len;
    return s->audio;
}

void sf2_audio_clear(Sf2 *s) { s->audio_len = 0; }
void sf2_set_sample_hook(Sf2 *s, Sf2SampleHook hook, void *user) {
    s->sample_hook = hook;
    s->sample_user = user;
}

void sf2_debug_jump(Sf2 *s, uint32_t pc) {
    /* entre cuadros la máquina está parada en un punto de cesión y se reanuda por la tabla de despacho */
    s->mc.cpu.pc = pc & 0xFFFFFFu;
    s->mc.skip_hook = 0;
}

uint64_t sf2_frame_count(const Sf2 *s) { return s->mc.cps.frame; }
uint64_t sf2_vblank_count(const Sf2 *s) { return s->mc.vblanks; }
int sf2_flip_requested(const Sf2 *s) { return s->flip; }

/* ---- estados guardados ------------------------------------------------------------------------- */
static void state_payload(Sf2 *s, StateStream *st) {
    machine_state(&s->mc, st);
    video_state(&s->video, st);
}

size_t sf2_state_size(Sf2 *s) {
    StateStream st;
    state_begin_save(&st, NULL, 0);
    state_payload(s, &st);
    return STATE_HEADER + st.pos;
}

uint32_t sf2_state_crc(Sf2 *s) {
    size_t n = sf2_state_size(s);
    if (n > s->scratch_cap) {
        uint8_t *b = realloc(s->scratch, n);
        if (!b)
            return 0;
        s->scratch = b;
        s->scratch_cap = n;
    }
    int w = sf2_state_save(s, s->scratch, s->scratch_cap);
    return w > (int)STATE_HEADER ? sf2_crc32(0, s->scratch + STATE_HEADER, (size_t)w - STATE_HEADER) : 0;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int sf2_state_save(Sf2 *s, uint8_t *buf, size_t cap) {
    if (cap < STATE_HEADER)
        return -1;
    StateStream st;
    state_begin_save(&st, buf + STATE_HEADER, cap - STATE_HEADER);
    state_payload(s, &st);
    if (st.error)
        return -1;
    memcpy(buf, STATE_MAGIC, 4);
    put32(buf + 4, STATE_VERSION);
    put32(buf + 8, recomp_rom_crc32);
    put32(buf + 12, (uint32_t)st.pos);
    put32(buf + 16, sf2_crc32(0, buf + STATE_HEADER, st.pos));
    put32(buf + 20, 0);
    return (int)(STATE_HEADER + st.pos);
}

static int load_payload(Sf2 *s, const uint8_t *payload, size_t len) {
    StateStream st;
    state_begin_load(&st, payload, len);
    state_payload(s, &st);
    return st.error || st.pos != len ? -1 : 0;
}

int sf2_state_load(Sf2 *s, const uint8_t *buf, size_t len, char *err, size_t errn) {
    if (len < STATE_HEADER || memcmp(buf, STATE_MAGIC, 4) != 0) {
        set_err(err, errn, "no es un estado guardado de SF2Recomp");
        return -1;
    }
    uint32_t version = get32(buf + 4), rom_crc = get32(buf + 8), plen = get32(buf + 12), pcrc = get32(buf + 16);
    if (version != STATE_VERSION) {
        set_err(err, errn, "estado de otra version del formato (%u, esta es la %u)", version, STATE_VERSION);
        return -1;
    }
    if (rom_crc != recomp_rom_crc32) {
        set_err(err, errn, "estado guardado con otro codigo recompilado (CRC %08x, este es %08x): viene de otras ROMs o de otra compilacion",
                rom_crc, recomp_rom_crc32);
        return -1;
    }
    if (plen != len - STATE_HEADER) {
        set_err(err, errn, "estado truncado o con datos de mas");
        return -1;
    }
    if (sf2_crc32(0, buf + STATE_HEADER, plen) != pcrc) {
        set_err(err, errn, "estado danado (el CRC del contenido no coincide)");
        return -1;
    }
    /* carga atómica: copia de seguridad del estado actual por si el contenido resulta inválido */
    size_t need = sf2_state_size(s);
    if (need > s->scratch_cap) {
        uint8_t *n = realloc(s->scratch, need);
        if (!n) { set_err(err, errn, "sin memoria"); return -1; }
        s->scratch = n;
        s->scratch_cap = need;
    }
    int backup = sf2_state_save(s, s->scratch, s->scratch_cap);
    if (load_payload(s, buf + STATE_HEADER, plen) != 0) {
        if (backup > 0)
            load_payload(s, s->scratch + STATE_HEADER, (size_t)backup - STATE_HEADER);
        set_err(err, errn, "estado invalido (un campo esta fuera de rango o falta contenido)");
        return -1;
    }
    s->audio_len = 0;
    s->shadow_next_ok = s->shadow_ready = 0;
    if (s->has_video)
        video_render(&s->video, &s->mc.mem);
    return 0;
}
