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
    /* ---- co-registration policy (all default to 0 = the classic behaviour) ---------------- */
    buttons_t synced_mask;     /* bits the window may DELAY; 0 = all. Others pass straight out. */
    buttons_t attack_mask;     /* bits that COUNT toward commit_at; 0 = all.                    */
    uint32_t  commit_at;       /* 0 = always ride out the window; N>=2 = commit at N pending.   */
    bool      grace_open;      /* an eager commit closed the window before its deadline...      */
    uint32_t  grace_until;     /* ...late joiners before THIS still land on that same frame.    */
    bool      preserve_width;  /* delay each release by the delay its OWN press was held        */
    buttons_t releasing;       /* committed bits whose release is waiting out that debt         */
    uint32_t  delay[32];       /* per bit: how long its commit held it back (ticks)             */
    uint32_t  rel_due[32];     /* per bit: when its delayed release lands                       */
    uint32_t  press_at[32];    /* per bit: when the press was first seen                        */
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

/* TWO MASKS, not one. `synced_mask` is what the window may hold back; `attack_mask` is what
 * counts as a chord. They are different questions and conflating them is a bug:
 *
 *   synced_mask  keep DIRECTIONS out of the window. A lever sweeping through a zone occupies
 *                it 1-3 ms, so a delayed direction is a dropped or fused direction, and every
 *                movement input pays the window for nothing. Co-registering a direction WITH a
 *                button is not worth buying: at 60 fps a frame is 16,700 us and the window at
 *                most 16,000, so they land on the same frame regardless.
 *
 *   attack_mask  which bits mean "a chord is forming". Without it, commit_at fires on
 *                direction+button and silently disables the window for anything that moves.
 *
 * Pass 0 for either to mean "all bits", so a zeroed struct behaves exactly as before. */
void      sync_window_set_masks(sync_window_t *s, buttons_t synced_mask, buttons_t attack_mask);

/* EAGER COMMIT (`commit_at`): fire as soon as N ATTACK bits are pending rather than always
 * riding out the window. This is a TRADE, not a free win:
 *
 *   the window is not idle time -- it IS the finger-gap tolerance. You cannot know at 0.3 ms
 *   that no third button is coming at 4 ms. Committing early does not reclaim waste, it
 *   SHORTENS the gap you tolerate.
 *
 * It counts PENDING, never HELD. Counting held bits would fire on every press made while
 * anything is already down -- most presses on a fight stick -- silently disabling the window.
 * A committed button has left `pending`, so holds are unaffected.
 *
 * The 3-button split this would otherwise cause is handled: an eager commit opens a GRACE
 * period running to the window's original deadline, and a press arriving inside it joins the
 * chord immediately instead of opening a fresh window. Without that, commit_at=2 makes a
 * three-button input land a frame late -- exactly what sync exists to prevent. Default 0 (off). */
void      sync_window_set_commit_at(sync_window_t *s, uint32_t commit_at);

/* PULSE WIDTH: by default a press is delayed by up to `window` while its release passes through
 * instantly, so the game sees every press up to a window SHORTER than it was held. With this on,
 * a release is held back by exactly the delay its own press incurred, so the width the game sees
 * is the width the player made -- just shifted.
 *
 * Honest cost model: at 60 fps a frame is 16,700 us and the window at most 16,000, so a 40 ms
 * press arriving as 35 ms crosses the same number of frames either way. This buys exactness, not
 * a felt difference. It is off by default for that reason.
 *
 * Mutually exclusive with release_debounce, which is a different release policy (defer EVERY
 * release by a full window). Asserted, not silently resolved. */
void      sync_window_set_preserve_width(sync_window_t *s, bool on);
buttons_t sync_window_step(sync_window_t *s, uint32_t now, buttons_t raw);

/* Ticks from `now` until the open window commits, or 0 if no window is open (nothing pending ->
 * sleep until an input edge). This is the timer-one-pulse deadline in software form. */
uint32_t  sync_window_next_commit(const sync_window_t *s, uint32_t now);

#endif /* NOBD_CORE_SYNC_WINDOW_H */
