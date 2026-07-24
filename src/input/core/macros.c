#include <assert.h>
#include <stddef.h>   /* NULL */
#include "macros.h"

/*
 * A faithful port of ../../input/macros.py -- button-name sets become buttons_t
 * masks and the macro/step tables are static. Keep the two in lockstep; input/dst.py
 * tier I12 proves they agree under fuzzing.
 */

static buttons_t buttons_at(const macro_t *m, uint32_t elapsed)
{
    uint32_t acc = 0;
    for (uint32_t i = 0; i < m->nsteps; i++) {
        acc += m->steps[i].ticks;
        if (elapsed < acc)
            return m->steps[i].buttons;
    }
    return 0;                                   /* past the end -- caller already guards */
}

void macro_player_init(macro_player_t *p)
{
    assert(p != NULL);
    p->nmacros = 0;
    p->triggers = 0;
    p->prev = 0;
    p->active = -1;
    p->start = 0;
    p->started = false;
    p->last_now = 0;
}

int32_t macro_add(macro_player_t *p, buttons_t trigger)
{
    assert(p != NULL);
    assert(trigger != 0);
    if (p->nmacros >= MACRO_MAX)
        return -1;
    int32_t idx = (int32_t)p->nmacros++;
    macro_t *m = &p->macros[idx];
    m->trigger = trigger;
    m->nsteps = 0;
    m->duration = 0;
    p->triggers |= trigger;
    return idx;
}

int32_t macro_add_step(macro_player_t *p, int32_t idx, buttons_t buttons, uint32_t ticks)
{
    assert(p != NULL);
    assert(idx >= 0 && (uint32_t)idx < p->nmacros);
    assert(ticks >= 1u);
    macro_t *m = &p->macros[idx];
    if (m->nsteps >= MACRO_MAX_STEPS)
        return -1;
    if (m->duration + ticks > MACRO_MAX_TICKS)
        return -1;
    m->steps[m->nsteps].buttons = buttons;
    m->steps[m->nsteps].ticks = ticks;
    m->nsteps++;
    m->duration += ticks;
    return 0;
}

buttons_t macro_step(macro_player_t *p, uint32_t now, buttons_t pressed)
{
    assert(p != NULL);
    assert(!p->started || now >= p->last_now);   /* time must not go backwards */

    buttons_t out = pressed;

    /* a trigger's RISING edge starts its macro, but only when nothing is playing */
    if (p->active < 0) {
        for (uint32_t i = 0; i < p->nmacros; i++) {
            buttons_t trig = p->macros[i].trigger;
            if ((pressed & trig) && !(p->prev & trig)) {
                p->active = (int32_t)i;
                p->start = now;
                break;
            }
        }
    }

    if (p->active >= 0) {
        const macro_t *m = &p->macros[p->active];
        uint32_t elapsed = now - p->start;
        if (elapsed >= m->duration) {            /* sequence finished -> idle */
            p->active = -1;
        } else {
            out &= ~p->triggers;                 /* a trigger press drives the macro, not the button */
            out |= buttons_at(m, elapsed);
        }
    }

    p->prev = pressed;
    p->started = true;
    p->last_now = now;
    /* negative space: playback is bounded -- never active past the macro's duration */
    assert(p->active < 0 || (now - p->start) < p->macros[p->active].duration);
    return out;
}

bool macro_player_playing(const macro_player_t *p)
{
    assert(p != NULL);
    assert(p->active >= -1 && p->active < (int32_t)p->nmacros);
    return p->active >= 0;
}
