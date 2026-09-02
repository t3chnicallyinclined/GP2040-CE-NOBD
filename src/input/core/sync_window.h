#ifndef NOBD_CORE_SYNC_WINDOW_H
#define NOBD_CORE_SYNC_WINDOW_H
#include <stdbool.h>
#include "buttons.h"

/*
 * NOBD sync window -- the co-registration front stage. Groups near-simultaneous
 * presses onto ONE output frame: a new press opens a window of `window` ticks, and
 * every press within that window commits together at the deadline (dashes,
 * throw-techs, multi-button inputs land on the same frame). This is the V2
 * equivalent of V1's syncGpioGetAll().
 *
 * Portable core module: this is the C the chip runs, and it compiles unchanged for
 * the host so the VOPR can fuzz it (input/dst.py tier I10 fuzzes it differentially
 * against ../../input/sync_window.py -- keep the two in lockstep).
 *
 * Cost model (honest): the window DELIBERATELY adds up to `window` ticks of latency
 * -- that is the trade for co-registration. Bounce rejection is SEPARATE and
 * upstream (hardware), not this stage.
 *
 * On-chip (golden rule -- offload from the CPU): the window is a countdown. Target =
 * a timer in one-pulse mode -- a press arms it for `window` ticks and it fires an
 * interrupt at the commit deadline, so the core sleeps (WFI) between, no polling.
 * This C is the reference.
 *
 * TigerStyle: static state, bounded (the window has a fixed deadline -- no unbounded
 * wait), asserts on entry + the negative space, named limits. `now` is a monotonic
 * u32 tick counter; re-epoch before it wraps at 2^32 ticks.
 */
#define SYNC_WINDOW_DEFAULT 5u      /* ticks (V1 default ~5 ms; map ms->ticks at call site) */
#define SYNC_WINDOW_MAX     500u    /* named limit (V1 nobdSyncDelay is 1..500)             */

typedef struct {
    uint32_t  window;
    bool      release_debounce;
    buttons_t committed;
    buttons_t pending;
    bool      open;
    bool      started;              /* has step() run yet (replaces Python's now=-1 sentinel) */
    uint32_t  deadline;
    uint32_t  last_now;
    buttons_t pending_release;      /* release_debounce: bits whose release is waiting out the window */
    bool      release_open;         /* a release window is counting down */
    uint32_t  release_deadline;
} sync_window_t;

void      sync_window_init(sync_window_t *s, uint32_t window, bool release_debounce);
buttons_t sync_window_step(sync_window_t *s, uint32_t now, buttons_t raw);

/* Ticks from `now` until the open window commits, or 0 if no window is open (nothing pending ->
 * sleep until an input edge). This is the timer-one-pulse deadline in software form. */
uint32_t  sync_window_next_commit(const sync_window_t *s, uint32_t now);

#endif /* NOBD_CORE_SYNC_WINDOW_H */
