#include <assert.h>
#include <stddef.h>   /* NULL */
#include "turbo.h"

/*
 * A faithful port of ../../input/turbo.py -- the per-button `_since` dict becomes a
 * fixed since[] table gated by a `tracking` mask (a bit is tracked iff it has been
 * held continuously and thus has a valid press tick). Keep the two in lockstep;
 * input/dst.py tier I11 proves they agree under fuzzing.
 */

void turbo_init(turbo_t *t, buttons_t buttons, uint32_t on_ticks, uint32_t off_ticks)
{
    assert(t != NULL);
    assert(on_ticks >= 1u && on_ticks <= TURBO_MAX_TICKS);
    assert(off_ticks >= 1u && off_ticks <= TURBO_MAX_TICKS);
    t->buttons = buttons;
    t->on_ticks = on_ticks;
    t->off_ticks = off_ticks;
    t->period = on_ticks + off_ticks;
    t->tracking = 0;
    t->started = false;
    t->last_now = 0;
    for (uint32_t i = 0; i < BUTTONS_BITS; i++)
        t->since[i] = 0;
}

buttons_t turbo_step(turbo_t *t, uint32_t now, buttons_t pressed)
{
    assert(t != NULL);
    assert(!t->started || now >= t->last_now);   /* time must not go backwards */

    buttons_t out = pressed;
    for (uint32_t i = 0; i < BUTTONS_BITS; i++) {
        buttons_t bit = (buttons_t)1u << i;
        if (!(t->buttons & bit))
            continue;                             /* not a turbo button */
        if (pressed & bit) {
            if (!(t->tracking & bit)) {           /* rising edge -> start ON this tick */
                t->tracking |= bit;
                t->since[i] = now;
            }
            assert(t->since[i] <= now);
            if ((now - t->since[i]) % t->period >= t->on_ticks)
                out &= ~bit;                      /* OFF portion of the pulse */
        } else {
            t->tracking &= ~bit;                  /* released -> forget phase */
        }
    }

    t->started = true;
    t->last_now = now;
    /* negative space: turbo only gates presses off, it never invents one */
    assert((out & ~pressed) == 0);
    return out;
}

uint32_t turbo_next_toggle(const turbo_t *t, uint32_t now)
{
    assert(t != NULL);
    assert(t->period >= 2u);                          /* on+off, each >= 1 */
    uint32_t best = 0u;                               /* 0 = no held turbo button -> nothing due */
    for (uint32_t i = 0; i < BUTTONS_BITS; i++) {
        buttons_t bit = (buttons_t)1u << i;
        if (!(t->tracking & bit))
            continue;                                 /* only a held turbo button toggles */
        assert(t->since[i] <= now);
        uint32_t phase = (now - t->since[i]) % t->period;
        /* ON while phase < on_ticks, OFF otherwise; the next toggle is the next boundary. */
        uint32_t until = (phase < t->on_ticks) ? (t->on_ticks - phase) : (t->period - phase);
        assert(until >= 1u && until <= t->period);    /* a toggle is always strictly ahead */
        if (best == 0u || until < best)
            best = until;
    }
    return best;
}
