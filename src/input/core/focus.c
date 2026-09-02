#include <assert.h>
#include <stddef.h>   /* NULL */
#include "focus.h"

/* Faithful port of ../../input/focus.py -- keep in lockstep (fuzzed via test_device.py). */

buttons_t focus_apply(buttons_t in, const focus_config_t *cfg)
{
    assert(cfg != NULL);

    buttons_t out = in & ~cfg->trigger;             /* the modifier is never an output */
    if (in & cfg->trigger)
        out &= ~cfg->disabled;                       /* focus held -> silence the set */

    assert((out & cfg->trigger) == 0u);
    return out;
}
