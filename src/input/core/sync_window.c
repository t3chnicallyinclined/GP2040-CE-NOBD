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
    s->pending_release = 0;
    s->release_open = false;
    s->release_deadline = 0;
}

buttons_t sync_window_step(sync_window_t *s, uint32_t now, buttons_t raw)
{
    assert(s != NULL);
    assert(!s->started || now > s->last_now);    /* time must strictly advance */

    /* releases: immediate by default; with release_debounce, a release waits out the
     * window symmetrically to a press (so a release co-registers/debounces like a press,
     * and a re-press inside the window cancels it). Mirrors V1 syncGpioGetAll(). */
    if (!s->release_debounce) {
        s->committed &= raw;                          /* keep only bits still held */
    } else {
        buttons_t just_released = s->committed & ~raw;/* committed but no longer held */
        if (just_released) {
            if (!s->release_open) {
                s->release_open = true;
                s->release_deadline = now + s->window;
            }
            s->pending_release |= just_released;
        }
        s->pending_release &= ~raw;                   /* a re-press cancels the pending release */
        if (s->pending_release && now >= s->release_deadline) {
            s->committed &= ~s->pending_release;
            s->pending_release = 0;
            s->release_open = false;
        }
        if (!s->pending_release)
            s->release_open = false;
    }

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
    assert(!(s->release_open && now >= s->release_deadline)); /* release window never past its deadline */
    assert(s->pending_release == 0 || s->release_open);       /* no pending release without an open window */
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
