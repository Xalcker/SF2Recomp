/* state.c - flujo de estados guardados (ver state.h). */
#include "state.h"
#include <string.h>

void state_begin_save(StateStream *s, uint8_t *buf, size_t cap) {
    s->buf = buf;
    s->cap = cap;
    s->pos = 0;
    s->saving = 1;
    s->error = 0;
}

void state_begin_load(StateStream *s, const uint8_t *buf, size_t len) {
    s->buf = (uint8_t *)buf;
    s->cap = len;
    s->pos = 0;
    s->saving = 0;
    s->error = 0;
}

void st_bytes(StateStream *s, void *p, size_t n) {
    if (s->saving) {
        if (s->buf) {
            if (n > s->cap - s->pos) { s->error = 1; return; }
            memcpy(s->buf + s->pos, p, n);
        }
        s->pos += n;
    } else {
        if (s->error || n > s->cap - s->pos) { s->error = 1; memset(p, 0, n); return; }
        memcpy(p, s->buf + s->pos, n);
        s->pos += n;
    }
}

static void st_le(StateStream *s, uint64_t *v, unsigned bytes) {
    uint8_t b[8];
    if (s->saving) {
        for (unsigned i = 0; i < bytes; i++) b[i] = (uint8_t)(*v >> (8 * i));
        st_bytes(s, b, bytes);
    } else {
        st_bytes(s, b, bytes);
        uint64_t x = 0;
        for (unsigned i = 0; i < bytes; i++) x |= (uint64_t)b[i] << (8 * i);
        *v = x;
    }
}

void st_u8(StateStream *s, uint8_t *v) { st_bytes(s, v, 1); }
void st_u16(StateStream *s, uint16_t *v) { uint64_t x = *v; st_le(s, &x, 2); *v = (uint16_t)x; }
void st_u32(StateStream *s, uint32_t *v) { uint64_t x = *v; st_le(s, &x, 4); *v = (uint32_t)x; }
void st_u64(StateStream *s, uint64_t *v) { st_le(s, v, 8); }
void st_i32(StateStream *s, int32_t *v) { uint32_t x = (uint32_t)*v; st_u32(s, &x); *v = (int32_t)x; }

void st_f32(StateStream *s, float *v) {
    uint32_t x;
    memcpy(&x, v, 4);
    st_u32(s, &x);
    memcpy(v, &x, 4);
}

void st_u16s(StateStream *s, uint16_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) st_u16(s, &p[i]);
}

void st_f32s(StateStream *s, float *p, size_t n) {
    for (size_t i = 0; i < n; i++) st_f32(s, &p[i]);
}

void st_check(StateStream *s, int ok) {
    if (!ok) s->error = 1;
}
