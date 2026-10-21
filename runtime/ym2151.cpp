// ym2151.cpp - envoltorio C del YM2151 de ymfm con temporizadores, ocupado y escrituras aplazadas
// resueltos como en el dispositivo de MAME (ver ym2151.h).
#include "ym2151.h"
#include "ymfm_opm.h"
#include "state.h"

#include <new>
#include <vector>

namespace {

struct Deferred {
    uint64_t time;
    int kind;          // 0 = escritura del registro de modo, 1 = recalcular la IRQ
    uint8_t data;
};

class Interface : public ymfm::ymfm_interface {
public:
    uint64_t now = 0;                 // ciclo del acceso o del suceso en curso
    uint64_t sync = 0;                // instante de aplicación de lo aplazado durante un acceso
    bool in_access = false;
    uint64_t busy_end = 0;
    uint64_t timer[2] = { YM2151_NEVER, YM2151_NEVER };
    Deferred queue[16] = {};          // las entradas sin usar quedan a cero (el estado guardado no depende de la basura de la memoria)
    int nqueue = 0;
    int irq = 0;

    ymfm::ymfm_engine_callbacks *engine() { return m_engine; }

    void push(uint64_t time, int kind, uint8_t data) {
        if (nqueue < 16)
            queue[nqueue++] = Deferred{ time, kind, data };
    }

    void ymfm_sync_mode_write(uint8_t data) override {
        // MAME: synchronize() siempre (también fuera de un acceso, aunque aquí no ocurre)
        push(in_access ? sync : now, 0, data);
    }
    void ymfm_sync_check_interrupts() override {
        if (in_access)
            push(sync, 1, 0);
        else
            m_engine->engine_check_interrupts();
    }
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override {
        timer[tnum & 1] = duration_in_clocks >= 0 ? now + (uint64_t)duration_in_clocks : YM2151_NEVER;
    }
    void ymfm_set_busy_end(uint32_t clocks) override { busy_end = now + clocks; }
    bool ymfm_is_busy() override { return now < busy_end; }
    void ymfm_update_irq(bool asserted) override { irq = asserted ? 1 : 0; }
};

} // namespace

struct Ym2151 {
    Interface intf;
    ymfm::ym2151 chip;
    Ym2151() : chip(intf) {}
};

extern "C" {

void ym2151_state(Ym2151 *y, StateStream *s) {
    Interface &in = y->intf;
    st_u64(s, &in.now);
    st_u64(s, &in.sync);
    uint8_t access = in.in_access ? 1 : 0;
    st_u8(s, &access);
    st_check(s, access <= 1);
    in.in_access = access != 0;
    st_u64(s, &in.busy_end);
    st_u64(s, &in.timer[0]);
    st_u64(s, &in.timer[1]);
    int32_t nq = in.nqueue;
    st_i32(s, &nq);
    st_check(s, nq >= 0 && nq <= 16);
    in.nqueue = (nq >= 0 && nq <= 16) ? nq : 0;
    for (int i = 0; i < 16; i++) {
        Deferred d = i < in.nqueue ? in.queue[i] : Deferred{ 0, 0, 0 };     // solo importan las nqueue primeras
        int32_t kind = d.kind;
        st_u64(s, &d.time);
        st_i32(s, &kind);
        st_check(s, kind == 0 || kind == 1);
        d.kind = kind;
        st_u8(s, &d.data);
        if (!s->saving)
            in.queue[i] = d;
    }
    int32_t irq = in.irq;
    st_i32(s, &irq);
    st_check(s, irq == 0 || irq == 1);
    in.irq = irq;
    /* el chip: ymfm serializa sus registros y contadores a un vector de bytes de tamaño fijo */
    std::vector<uint8_t> cur;
    {
        ymfm::ymfm_saved_state st(cur, true);
        y->chip.save_restore(st);
    }
    uint32_t n = (uint32_t)cur.size();
    st_u32(s, &n);
    st_check(s, n == cur.size());
    if (s->saving) {
        st_bytes(s, cur.data(), cur.size());
    } else if (!s->error) {
        std::vector<uint8_t> buf(cur.size());
        st_bytes(s, buf.data(), buf.size());
        if (!s->error) {
            ymfm::ymfm_saved_state st(buf, false);
            y->chip.save_restore(st);
        }
    }
}

Ym2151 *ym2151_new(void) {
    return new (std::nothrow) Ym2151();
}

void ym2151_free(Ym2151 *y) {
    delete y;
}

void ym2151_reset(Ym2151 *y) {
    y->intf.now = 0;
    y->intf.in_access = false;
    y->intf.nqueue = 0;
    y->chip.reset();
}

void ym2151_write(Ym2151 *y, uint64_t now, uint64_t sync, unsigned offset, uint8_t data) {
    y->intf.now = now;
    y->intf.sync = sync;
    y->intf.in_access = true;
    y->chip.write(offset, data);
    y->intf.in_access = false;
}

uint8_t ym2151_read(Ym2151 *y, uint64_t now, uint64_t sync, unsigned offset) {
    y->intf.now = now;
    y->intf.sync = sync;
    y->intf.in_access = true;
    uint8_t v = y->chip.read(offset);
    y->intf.in_access = false;
    return v;
}

uint64_t ym2151_next_event(const Ym2151 *y) {
    uint64_t t = y->intf.timer[0] < y->intf.timer[1] ? y->intf.timer[0] : y->intf.timer[1];
    for (int i = 0; i < y->intf.nqueue; i++)
        if (y->intf.queue[i].time < t)
            t = y->intf.queue[i].time;
    return t;
}

void ym2151_run_event(Ym2151 *y) {
    Interface &in = y->intf;
    uint64_t t = ym2151_next_event(y);
    if (t == YM2151_NEVER)
        return;
    in.now = t;
    // a igual instante, primero los temporizadores (creados antes) y luego lo aplazado, en orden de llegada
    for (int tn = 0; tn < 2; tn++)
        if (in.timer[tn] == t) {
            in.timer[tn] = YM2151_NEVER;
            in.engine()->engine_timer_expired(tn);
            return;
        }
    for (int i = 0; i < in.nqueue; i++)
        if (in.queue[i].time == t) {
            Deferred d = in.queue[i];
            for (int j = i + 1; j < in.nqueue; j++)
                in.queue[j - 1] = in.queue[j];
            in.nqueue--;
            if (d.kind == 0)
                in.engine()->engine_mode_write(d.data);
            else
                in.engine()->engine_check_interrupts();
            return;
        }
}

int ym2151_irq(const Ym2151 *y) {
    return y->intf.irq;
}

void ym2151_generate(Ym2151 *y, int32_t *lr, unsigned n) {
    ymfm::ym2151::output_data out[64];
    while (n) {
        unsigned k = n < 64 ? n : 64;
        y->chip.generate(out, k);
        for (unsigned i = 0; i < k; i++) {
            lr[2 * i] = out[i].data[0];
            lr[2 * i + 1] = out[i].data[1];
        }
        lr += 2 * k;
        n -= k;
    }
}

} // extern "C"
