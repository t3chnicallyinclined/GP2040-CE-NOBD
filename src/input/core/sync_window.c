#include <assert.h>
#include <stddef.h>   /* NULL */
#include "sync_window.h"

/*
 * A faithful port of ../../input/sync_window.py -- set operations become bitmask
 * operations on `buttons_t`. Keep the two in lockstep; input/dst.py tier I10 proves
 * they agree under fuzzing.
 */

void sync_window_init(sync_window_t *s, uint32_t window, bool release_debounce)
{
    assert(s != NULL);
    assert(window >= 1u && window <= SYNC_WINDOW_MAX);
    s->window = window;
    s->release_debounce = release_debounce;
    s->committed = 0;
    s->pending = 0;
    s->open = false;
    s->started = false;
    s->deadline = 0;
    s->last_now = 0;
}

buttons_t sync_window_step(sync_window_t *s, uint32_t now, buttons_t raw)
{
    assert(s != NULL);
    assert(!s->started || now > s->last_now);    /* time must strictly advance */

    /* releases take effect immediately (release-debounce off by default):
     * keep only committed bits that are still held. */
    if (!s->release_debounce)
        s->committed &= raw;

    /* a new press (not committed, not already pending) opens or joins a window */
    buttons_t fresh = raw & ~s->committed & ~s->pending;
    if (fresh) {
        if (!s->open) {
            s->open = true;
            s->deadline = now + s->window;
        }
        s->pending |= fresh;
    }

    /* commit everything the window collected, at its deadline */
    if (s->open && now >= s->deadline) {
        s->committed |= s->pending;
        s->pending = 0;
        s->open = false;
    }

    s->started = true;
    s->last_now = now;
    /* negative space: a window is never left open past its deadline, and there is
     * never pending state without an open window. */
    assert(!(s->open && now >= s->deadline));
    assert(s->pending == 0 || s->open);
    return s->committed;
}

uint32_t sync_window_next_commit(const sync_window_t *s, uint32_t now)
{
    assert(s != NULL);
    if (!s->open)
        return 0u;                            /* no window open -> nothing pending */
    assert(s->deadline > now);                /* step commits+closes once now >= deadline */
    return s->deadline - now;                 /* >= 1: the commit is strictly ahead */
}
