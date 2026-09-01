#include <assert.h>
#include <stddef.h>   /* NULL */
#include "sync_window.h"
#include "core_hot.h"

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
    s->synced_mask = 0;        /* 0 = all bits, i.e. the classic behaviour */
    s->attack_mask = 0;
    s->commit_at = 0;
    s->grace_open = false;
    s->grace_until = 0;
    s->preserve_width = false;
    s->releasing = 0;
    for (unsigned b = 0; b < 32u; b++) { s->delay[b] = 0; s->rel_due[b] = 0; s->press_at[b] = 0; }
}

void sync_window_set_preserve_width(sync_window_t *s, bool on)
{
    assert(s != NULL);
    assert(!(on && s->release_debounce));   /* two different release policies; pick one */
    s->preserve_width = on;
}

/* How long this commit held each bit back. Clamped to the window BY CONTRACT: a press is never
 * delayed longer than that, so a stalled source or a sparse tick cannot compute a release
 * deadline minutes away and hang the button down. */
static void sw_record_delay(sync_window_t *s, buttons_t bits, uint32_t now)
{
    if (!s->preserve_width) return;
    while (bits) {
        const unsigned b = (unsigned)__builtin_ctz((unsigned)bits);
        const uint32_t held = now - s->press_at[b];
        s->delay[b] = held > s->window ? s->window : held;
        bits &= bits - 1u;
    }
}

void sync_window_set_masks(sync_window_t *s, buttons_t synced_mask, buttons_t attack_mask)
{
    assert(s != NULL);
    s->synced_mask = synced_mask;
    s->attack_mask = attack_mask;
}

void sync_window_set_commit_at(sync_window_t *s, uint32_t commit_at)
{
    assert(s != NULL);
    assert(commit_at == 0u || commit_at >= 2u);   /* 1 would commit every press instantly */
    s->commit_at = commit_at;
}

buttons_t CORE_HOT(sync_window_step)(sync_window_t *s, uint32_t now, buttons_t raw)
{
    assert(s != NULL);
    assert(!s->started || now >= s->last_now);   /* monotonic: repeats OK (V1 polls faster than a tick), never backward */

    /* Bits outside synced_mask are never held back -- they bypass the window entirely and are
     * OR'd back on at the end. 0 means "all bits are synced", so a zeroed struct is unchanged. */
    const buttons_t sm       = s->synced_mask ? s->synced_mask : (buttons_t)~(buttons_t)0;
    const buttons_t passthru = raw & ~sm;
    raw &= sm;

    /* releases: immediate by default; with release_debounce, a release waits out the
     * window symmetrically to a press (so a release co-registers/debounces like a press,
     * and a re-press inside the window cancels it). Mirrors V1 syncGpioGetAll(). */
    if (s->preserve_width) {
        /* A committed bit that goes up owes exactly the delay its own press was held by --
         * that is what makes the width the game sees the width the player made. */
        buttons_t just_released = s->committed & ~raw & ~s->releasing;
        while (just_released) {
            const unsigned b = (unsigned)__builtin_ctz((unsigned)just_released);
            s->rel_due[b] = now + s->delay[b];
            s->releasing |= (buttons_t)1u << b;
            just_released &= just_released - 1u;
        }
        s->releasing &= ~raw;        /* pressed again before its debt ran out: one continuous press */
    } else if (!s->release_debounce) {
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

    /* A press released BEFORE its window commits is dropped -- never co-registered. Prune pending
     * to bits still held (mirrors V1 syncGpioGetAll's `sync_new &= raw_buttons`). Without this,
     * rapid direction taps (MvC2 tri-dash / wavedash / piano, all faster than the window) accumulate
     * and ALL commit at the deadline -> phantom opposing directions -> the d-pad STICKS until a
     * release step strips them. This is the regression the ad-hoc->core port introduced; the release
     * half was aligned in 19d8ad0a but the press half was not, and the realistic-input DST missed it. */
    s->pending &= raw;

    /* The grace period from an eager commit runs to the ORIGINAL deadline, then expires. */
    if (s->grace_open && now >= s->grace_until)
        s->grace_open = false;

    /* a new press (not committed, not already pending) opens or joins a window */
    buttons_t fresh = raw & ~s->committed & ~s->pending;
    if (fresh) {
        buttons_t f = fresh;
        while (f) {                                   /* remember when each press was seen */
            const unsigned b = (unsigned)__builtin_ctz((unsigned)f);
            s->press_at[b] = now;
            f &= f - 1u;
        }
        if (s->grace_open) {
            /* An eager commit already sent this chord out and closed the window early. A press
             * still inside that window belongs to the SAME input, so publish it now rather than
             * opening a fresh window and landing it a frame later. This is what stops commit_at
             * from splitting a three-button press. */
            sw_record_delay(s, fresh, now);
            s->committed |= fresh;
        } else {
            if (!s->open) {
                s->open = true;
                s->deadline = now + s->window;
            }
            s->pending |= fresh;
        }
    }

    /* Eager commit: enough ATTACK bits are pending that there is nothing left to wait for.
     * Counts pending, never held -- see the header. */
    if (s->open && s->commit_at != 0u) {
        const buttons_t am = s->attack_mask ? s->attack_mask : (buttons_t)~(buttons_t)0;
        if ((uint32_t)__builtin_popcount((unsigned)(s->pending & am)) >= s->commit_at) {
            if (now < s->deadline) {          /* early: hold the door open for late joiners */
                s->grace_open  = true;
                s->grace_until = s->deadline;
            }
            sw_record_delay(s, s->pending, now);
            s->committed |= s->pending;
            s->pending = 0;
            s->open = false;
        }
    }

    /* commit everything the window collected, at its deadline */
    if (s->open && now >= s->deadline) {
        sw_record_delay(s, s->pending, now);
        s->committed |= s->pending;
        s->pending = 0;
        s->open = false;
    }

    /* releases whose debt has run out */
    if (s->preserve_width) {
        buttons_t r = s->releasing;
        while (r) {
            const unsigned b = (unsigned)__builtin_ctz((unsigned)r);
            if (now >= s->rel_due[b]) {
                s->committed &= ~((buttons_t)1u << b);
                s->releasing &= ~((buttons_t)1u << b);
            }
            r &= r - 1u;
        }
    }

    s->started = true;
    s->last_now = now;
    /* negative space: a window is never left open past its deadline, and there is
     * never pending state without an open window. */
    assert(!(s->open && now >= s->deadline));
    assert(s->pending == 0 || s->open);
    assert(!(s->release_open && now >= s->release_deadline)); /* release window never past its deadline */
    assert(s->pending_release == 0 || s->release_open);       /* no pending release without an open window */
    assert(!(s->grace_open && now >= s->grace_until));        /* grace never outlives its deadline */
    return s->committed | passthru;
}

uint32_t sync_window_next_commit(const sync_window_t *s, uint32_t now)
{
    assert(s != NULL);
    if (!s->open)
        return 0u;                            /* no window open -> nothing pending */
    assert(s->deadline > now);                /* step commits+closes once now >= deadline */
    return s->deadline - now;                 /* >= 1: the commit is strictly ahead */
}
