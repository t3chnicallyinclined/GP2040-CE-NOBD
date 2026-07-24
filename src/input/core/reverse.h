#ifndef NOBD_CORE_REVERSE_H
#define NOBD_CORE_REVERSE_H
#include <stdbool.h>
#include "buttons.h"

/*
 * Reverse -- swap the d-pad axes (UP<->DOWN, LEFT<->RIGHT), optionally only while a
 * modifier button is held. A stateless output-processing stage. Runs AFTER remap and
 * BEFORE SOCD, so SOCD still cleans whatever the reversal produces. Mirrors
 * ../../input/reverse.py (fuzzed differentially via firmware/app/test_device.py).
 *
 * `trigger` is a dedicated modifier: 0 = always reversed; else reverse only while that
 * button is held, and the modifier itself is masked out of the output (it is not a game
 * button). `ud`/`lr` pick which axes to reverse.
 */
typedef struct {
    bool      ud;         /* reverse UP <-> DOWN */
    bool      lr;         /* reverse LEFT <-> RIGHT */
    buttons_t trigger;    /* 0 = always; else active only while held (and masked out) */
} reverse_config_t;

buttons_t reverse_apply(buttons_t in, const reverse_config_t *cfg);

#endif /* NOBD_CORE_REVERSE_H */
