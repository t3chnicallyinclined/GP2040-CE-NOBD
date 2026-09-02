#ifndef NOBD_CORE_TURBO_H
#define NOBD_CORE_TURBO_H
#include <stdbool.h>
#include "buttons.h"

/*
 * Turbo (auto-fire) -- a post-SOCD output stage. While a turbo-enabled button is
 * held, its OUTPUT is pulsed on/off at a fixed rate, so one hold becomes repeated
 * presses. Phase is derived from ABSOLUTE time ((now - pressed_at) % period), so the
 * rising edge is always ON (no dropped first frame) and a jump in `now` still lands
 * on the right phase. Turbo only ever gates a press OFF, never fabricates one
 * (out is a subset of in). Mirrors ../../input/turbo.py (fuzzed differentially by
 * input/dst.py tier I11).
 *
 * On-chip (golden rule -- offload from the CPU): a fixed-rate on/off pulse is just a
 * timer PWM channel. Target = an autonomous timer PWM per turbo button (zero CPU per
 * tick, ~40 CC channels available); this C is the reference the waveform must match,
 * and the software fallback.
 *
 * TigerStyle: static per-button state (a fixed since[] table), bounded (one pass
 * over the 32 bits), asserts + the never-fabricate negative space, named limits.
 */
#define TURBO_MAX_TICKS 255u        /* per-phase cap (fits a u8 counter on-chip) */

typedef struct {
    buttons_t buttons;              /* which bits auto-fire */
    uint32_t  on_ticks;
    uint32_t  off_ticks;
    uint32_t  period;               /* on_ticks + off_ticks (cached) */
    buttons_t tracking;             /* bits with a valid since[] (held continuously) */
    uint32_t  since[BUTTONS_BITS];  /* tick each bit was pressed (valid iff in tracking) */
    bool      started;
    uint32_t  last_now;
} turbo_t;

void      turbo_init(turbo_t *t, buttons_t buttons, uint32_t on_ticks, uint32_t off_ticks);
buttons_t turbo_step(turbo_t *t, uint32_t now, buttons_t pressed);

/* Ticks from `now` until the next output toggle of the soonest currently-held turbo button, or 0
 * if none is held (nothing pending -> the core can sleep until an input edge). Lets the event-driven
 * wake sleep exactly to the next auto-fire edge instead of polling every tick. */
uint32_t  turbo_next_toggle(const turbo_t *t, uint32_t now);

#endif /* NOBD_CORE_TURBO_H */
