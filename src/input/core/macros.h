#ifndef NOBD_CORE_MACROS_H
#define NOBD_CORE_MACROS_H
#include <stdbool.h>
#include "buttons.h"

/*
 * Macros -- a post-SOCD output stage that plays a recorded input sequence when a
 * trigger button is pressed. On the RISING edge of a trigger, one macro plays once
 * (one-shot): each step drives its button set for a fixed number of ticks, then the
 * next, until the sequence ends. ONE macro plays at a time, so playback is bounded
 * by the longest single macro. Trigger buttons are removed from the live output
 * during playback. Mirrors ../../input/macros.py (fuzzed differentially by
 * input/dst.py tier I12).
 *
 * Construction is a static builder -- macro_add() then macro_add_step() -- mirroring
 * the Python macro() helper + MacroPlayer([...]). Everything is fixed-size: no
 * allocation, so the whole player lives in a single static struct.
 *
 * On-chip (golden rule -- offload from the CPU): event-driven on the V3F core --
 * playback is armed by a trigger's edge (EXTI) and advanced by the tick timer; no
 * polling when idle.
 *
 * TigerStyle: static state (fixed macro + step tables), bounded (playback never
 * exceeds a macro's duration -- the negative-space assert), asserts on entry, and a
 * named cap on macros / steps / duration.
 */
#define MACRO_MAX        32u        /* macros one player can hold */
#define MACRO_MAX_STEPS  64u        /* steps per macro            */
#define MACRO_MAX_TICKS  6000u      /* per-macro total-duration cap */

typedef struct {
    buttons_t buttons;
    uint32_t  ticks;
} macro_step_t;

typedef struct {
    buttons_t    trigger;                    /* the trigger BIT (a single button) */
    uint32_t     nsteps;
    uint32_t     duration;                   /* cached sum of step ticks */
    macro_step_t steps[MACRO_MAX_STEPS];
} macro_t;

typedef struct {
    uint32_t  nmacros;
    macro_t   macros[MACRO_MAX];
    buttons_t triggers;                      /* OR of all trigger bits (cached) */
    buttons_t prev;                          /* previous pressed mask (edge detect) */
    int32_t   active;                        /* index of the playing macro, or -1 */
    uint32_t  start;                         /* tick playback started */
    bool      started;
    uint32_t  last_now;
} macro_player_t;

void      macro_player_init(macro_player_t *p);
int32_t   macro_add(macro_player_t *p, buttons_t trigger);                       /* -> index, or -1 if full */
int32_t   macro_add_step(macro_player_t *p, int32_t idx, buttons_t buttons, uint32_t ticks); /* 0 ok / -1 full */
buttons_t macro_step(macro_player_t *p, uint32_t now, buttons_t pressed);

/* True while a macro is playing back (needs per-tick servicing until it finishes). Lets the
 * event-driven wake tell "a macro is running" from "idle, waiting for a trigger". */
bool      macro_player_playing(const macro_player_t *p);

#endif /* NOBD_CORE_MACROS_H */
