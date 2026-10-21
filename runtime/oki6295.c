/* oki6295.c - OKI MSM6295 (ver oki6295.h). */
#include "oki6295.h"
#include "state.h"
#include <string.h>

static int diff_lookup[49 * 16];
static int tables_ready;

static const int8_t index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* atenuación del segundo byte del comando: 0, -3,2, -6, -9,2, -12, -14,5, -18, -20,5, -24 dB, resto silencio */
static const float volume_table[16] = {
    0x20 / 32.0f, 0x16 / 32.0f, 0x10 / 32.0f, 0x0b / 32.0f, 0x08 / 32.0f, 0x06 / 32.0f, 0x04 / 32.0f,
    0x03 / 32.0f, 0x02 / 32.0f, 0, 0, 0, 0, 0, 0, 0,
};

/* paso de la señal: floor(16 * 1,1^n) para n = 0..48.  Es la tabla de okiadpcm.cpp de MAME, que la calcula con
 * pow() al arrancar; aquí va escrita para no depender de que la libm de cada plataforma redondee igual */
const int oki6295_step_table[49] = {
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
};

static void compute_tables(void) {
    /* bits de cada nibble: signo y las tres magnitudes (paso, paso/2, paso/4) más paso/8 siempre */
    static const int8_t nbl2bit[16][4] = {
        { 1, 0, 0, 0 }, { 1, 0, 0, 1 }, { 1, 0, 1, 0 }, { 1, 0, 1, 1 },
        { 1, 1, 0, 0 }, { 1, 1, 0, 1 }, { 1, 1, 1, 0 }, { 1, 1, 1, 1 },
        { -1, 0, 0, 0 }, { -1, 0, 0, 1 }, { -1, 0, 1, 0 }, { -1, 0, 1, 1 },
        { -1, 1, 0, 0 }, { -1, 1, 0, 1 }, { -1, 1, 1, 0 }, { -1, 1, 1, 1 },
    };
    for (int step = 0; step <= 48; step++) {
        int stepval = oki6295_step_table[step];
        for (int nib = 0; nib < 16; nib++)
            diff_lookup[step * 16 + nib] = nbl2bit[nib][0] *
                (stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] + stepval / 4 * nbl2bit[nib][3] + stepval / 8);
    }
    tables_ready = 1;
}

void oki6295_init(Oki6295 *o, const uint8_t *rom) {
    if (!tables_ready)
        compute_tables();
    memset(o, 0, sizeof *o);
    o->rom = rom;
    o->command = -1;
    o->pin7 = 1;
}

void oki6295_state(Oki6295 *o, StateStream *s) {
    int32_t command = o->command, pin7 = o->pin7;
    st_i32(s, &command);
    st_check(s, command >= -1 && command <= 127);
    st_i32(s, &pin7);
    st_check(s, pin7 == 0 || pin7 == 1);
    o->command = command;
    o->pin7 = pin7;
    for (int v = 0; v < 4; v++) {
        OkiVoice *vo = &o->voice[v];
        int32_t playing = vo->playing;
        st_i32(s, &playing);
        st_check(s, playing == 0 || playing == 1);
        vo->playing = playing;
        st_u32(s, &vo->base);
        st_check(s, vo->base <= 0x3ffff);
        st_u32(s, &vo->sample);
        st_u32(s, &vo->count);
        st_check(s, vo->count <= 0x80000 && vo->sample <= vo->count);
        st_i32(s, &vo->signal);
        st_check(s, vo->signal >= -2048 && vo->signal <= 2047);
        st_i32(s, &vo->step);
        st_check(s, vo->step >= 0 && vo->step <= 48);
        st_f32(s, &vo->volume);
    }
}

void oki6295_reset(Oki6295 *o) {
    for (int i = 0; i < 4; i++)
        o->voice[i].playing = 0;
}

static inline uint8_t rom_byte(const Oki6295 *o, uint32_t addr) {
    return o->rom[addr & (OKI6295_ROM_SIZE - 1)];
}

void oki6295_write(Oki6295 *o, uint8_t data) {
    if (o->command != -1) {
        int mask = data >> 4;
        for (int v = 0; v < 4; v++, mask >>= 1) {
            if (!(mask & 1))
                continue;
            OkiVoice *vo = &o->voice[v];
            if (vo->playing) { o->rejected++; continue; }
            uint32_t base = (uint32_t)o->command * 8;
            uint32_t start = ((uint32_t)rom_byte(o, base) << 16 | (uint32_t)rom_byte(o, base + 1) << 8 | rom_byte(o, base + 2)) & 0x3ffff;
            uint32_t stop = ((uint32_t)rom_byte(o, base + 3) << 16 | (uint32_t)rom_byte(o, base + 4) << 8 | rom_byte(o, base + 5)) & 0x3ffff;
            if (start < stop) {
                vo->playing = 1;
                vo->base = start;
                vo->sample = 0;
                vo->count = 2 * (stop - start + 1);
                vo->signal = 0;
                vo->step = 0;
                vo->volume = volume_table[data & 0x0f];
                o->starts++;
            } else {
                o->rejected++;
            }
        }
        o->command = -1;
    } else if (data & 0x80) {
        o->command = data & 0x7f;
    } else {
        int mask = data >> 3;
        for (int v = 0; v < 4; v++, mask >>= 1)
            if (mask & 1) {
                if (o->voice[v].playing) o->stops++;
                o->voice[v].playing = 0;
            }
    }
}

uint8_t oki6295_read(const Oki6295 *o) {
    uint8_t r = 0xf0;
    for (int v = 0; v < 4; v++)
        if (o->voice[v].playing)
            r |= (uint8_t)(1u << v);
    return r;
}

float oki6295_generate(Oki6295 *o) {
    float out = 0.0f;
    for (int v = 0; v < 4; v++) {
        OkiVoice *vo = &o->voice[v];
        if (!vo->playing)
            continue;
        int nibble = rom_byte(o, vo->base + vo->sample / 2) >> (((vo->sample & 1) << 2) ^ 4);
        vo->signal += diff_lookup[vo->step * 16 + (nibble & 15)];
        if (vo->signal > 2047) vo->signal = 2047;
        else if (vo->signal < -2048) vo->signal = -2048;
        vo->step += index_shift[nibble & 7];
        if (vo->step > 48) vo->step = 48;
        else if (vo->step < 0) vo->step = 0;
        /* MAME: stream.add_int(0, i, int16 * float, 2048) -> (s32) truncado y luego double / 2048 */
        int32_t s = (int32_t)((float)(int16_t)vo->signal * vo->volume);
        out += (float)((double)s / 2048);
        if (++vo->sample >= vo->count)
            vo->playing = 0;
    }
    return out;
}
