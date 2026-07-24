#include <assert.h>
#include <stddef.h>   /* NULL */
#include "reverse.h"

/* Faithful port of ../../input/reverse.py -- keep in lockstep (fuzzed via test_device.py). */

buttons_t reverse_apply(buttons_t in, const reverse_config_t *cfg)
{
    assert(cfg != NULL);

    buttons_t out = in;
    bool active = (cfg->trigger == 0u) || ((in & cfg->trigger) != 0u);

    if (cfg->trigger != 0u)
        out &= ~cfg->trigger;                       /* the modifier is not a game output */

    if (active) {
        if (cfg->ud) {
            buttons_t ud = out & (BTN_UP | BTN_DOWN);
            out &= ~(BTN_UP | BTN_DOWN);
            if (ud & BTN_UP)   out |= BTN_DOWN;
            if (ud & BTN_DOWN) out |= BTN_UP;
        }
        if (cfg->lr) {
            buttons_t lr = out & (BTN_LEFT | BTN_RIGHT);
            out &= ~(BTN_LEFT | BTN_RIGHT);
            if (lr & BTN_LEFT)  out |= BTN_RIGHT;
            if (lr & BTN_RIGHT) out |= BTN_LEFT;
        }
    }

    assert(cfg->trigger == 0u || (out & cfg->trigger) == 0u);   /* the modifier never survives */
    return out;
}
