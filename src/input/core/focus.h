#ifndef NOBD_CORE_FOCUS_H
#define NOBD_CORE_FOCUS_H
#include "buttons.h"

/*
 * Focus mode -- while a modifier button is held, silence a configured set of buttons
 * (e.g. lock out Start/Select so they can't be hit mid-match). A stateless stage that
 * runs LAST, so it masks the fully-resolved output. The modifier is dedicated and never
 * reaches the output. Mirrors ../../input/focus.py (fuzzed via test_device.py).
 */
typedef struct {
    buttons_t trigger;    /* held -> focus active; never an output itself */
    buttons_t disabled;   /* buttons silenced while focus is held */
} focus_config_t;

buttons_t focus_apply(buttons_t in, const focus_config_t *cfg);

#endif /* NOBD_CORE_FOCUS_H */
