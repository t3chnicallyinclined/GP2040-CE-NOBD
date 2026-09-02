/*
 * test/sync_equiv.c -- differential: the vendored core sync window (src/input/core/
 * sync_window.c) vs GP2040-NOBD's incumbent syncGpioGetAll (src/platform/gp2040.cpp).
 *
 * WHY: before swapping the core sync into V1's slot, prove it matches the intended
 * (fork) behavior -- co-registration AND the release_debounce toggle. The core's DST
 * (I10) only proves C==Python; it can't catch a defect the Python spec SHARES. This
 * differential vs the incumbent is exactly what catches that -- e.g. the suspected
 * release_debounce=true "never releases" bug.
 *
 * MODEL: both are driven with the SAME integer time `now` and window (fork's us and
 * the core's ticks collapse to one unit here). now advances strictly (the core asserts
 * it); co-registration is exercised by pressing across several steps within `window`.
 * The V1-polling repeated-`now` case is an INTEGRATION concern handled at swap time
 * (relax the strict-advance assert), separate from this logic check.
 *
 * Build: zig cc test/sync_equiv.c src/input/core/sync_window.c -I src/input/core -O2 -o sync_equiv
 */
#include <stdint.h>
#include <stdio.h>
#include "sync_window.h"   /* core: sync_window_t, sync_window_init/step */

/* ---- incumbent, transcribed VERBATIM from gp2040.cpp syncGpioGetAll() (lines 327-367),
 * buttonGpios = all bits. Its 5 function-statics become explicit fields so the test can
 * reset per run; logic otherwise identical. release_start==0 is the fork's "unset"
 * sentinel, so the driver starts `now` at 1. Keep in sync with the fork. ---- */
typedef struct {
    int      sync_pending;
    uint32_t sync_start;
    uint32_t sync_new;
    uint32_t pending_release;
    uint32_t release_start;   /* 0 == unset (fork sentinel) */
    uint32_t debounced;
} fork_t;

static uint32_t fork_step(fork_t *s, uint32_t now, uint32_t raw, uint32_t delay, int relDeb) {
    if (!s->sync_pending && !s->pending_release && s->debounced == raw) return s->debounced; /* early-out */
    uint32_t prev = s->debounced;
    uint32_t just_pressed  = raw & ~prev & ~s->sync_new;
    uint32_t just_released = prev & ~raw;

    if (relDeb) {
        if (just_released) { s->pending_release |= just_released; if (s->release_start == 0) s->release_start = now; }
        s->pending_release &= ~raw;
        if (s->pending_release && (now - s->release_start) >= delay) {
            s->debounced &= ~s->pending_release; s->pending_release = 0; s->release_start = 0;
        }
        if (!s->pending_release) s->release_start = 0;
    } else {
        if (just_released) s->debounced &= ~just_released;
    }

    s->sync_new &= raw;
    if (just_pressed) {
        if (!s->sync_pending) { s->sync_pending = 1; s->sync_start = now; s->sync_new = just_pressed; }
        else s->sync_new |= just_pressed;
    }
    if (s->sync_pending && (now - s->sync_start) >= delay) {
        s->debounced |= s->sync_new; s->sync_pending = 0; s->sync_new = 0;
    }
    return s->debounced;
}

static int run_case(uint32_t delay, int relDeb, uint32_t seed, uint32_t hold) {
    fork_t f = {0};
    sync_window_t c; sync_window_init(&c, delay, relDeb);
    uint32_t now = 1, rng = seed;
    uint32_t raw = 0;
    int fails = 0;
    for (uint32_t i = 0; i < 200000u && fails < 3; i++) {
        rng = rng * 1664525u + 1013904223u;
        if (i % hold == 0u)                        /* change state every `hold` ticks */
            raw ^= (rng >> 11) & 0x3Fu;            /* (hold >> window => realistic; hold=1 => adversarial) */
        now += 1u;                                 /* strictly advance one tick per step */

        uint32_t fo = fork_step(&f, now, raw, delay, relDeb);
        uint32_t co = sync_window_step(&c, now, raw);
        if ((fo & 0x3Fu) != (co & 0x3Fu)) {
            printf("      MISMATCH i=%u now=%u raw=0x%02X  fork=0x%02X core=0x%02X\n",
                   i, now, raw & 0x3F, fo & 0x3F, co & 0x3F);
            fails++;
        }
    }
    return fails;
}

int main(void) {
    printf("sync-window equivalence: core (sync_window.c) vs incumbent (syncGpioGetAll)\n");
    const uint32_t delays[] = {3u, 5u, 10u};
    const struct { const char *name; uint32_t hold; } modes[] = {
        { "realistic  (buttons held 40 ticks >> window)", 40u },
        { "adversarial(toggle every tick, sub-window taps)", 1u },
    };
    for (unsigned mo = 0; mo < 2; mo++) {
        printf("  -- %s --\n", modes[mo].name);
        for (int r = 0; r <= 1; r++) {
            int fails = 0;
            for (unsigned d = 0; d < sizeof(delays)/sizeof(delays[0]); d++)
                fails += run_case(delays[d], r, 0x51ED17u ^ (delays[d] << 3) ^ (uint32_t)r, modes[mo].hold);
            printf("    [release_debounce=%s] %s\n", r ? "true " : "false", fails ? "FAIL" : "PASS");
        }
    }
    return 0;  /* informational: we want the per-mode breakdown, not a pass/fail gate */
}
