#include <assert.h>
#include <stddef.h>   /* NULL */
#include "hotkeys.h"

/* A faithful port of ../../input/hotkeys.py -- combo edge detection + output masking. */

void hotkeys_init(hotkeys_t *h)
{
    assert(h != NULL);
    h->count = 0;
    h->prev = 0;
}

int32_t hotkeys_add(hotkeys_t *h, buttons_t combo, hotkey_action_t action, uint8_t param)
{
    assert(h != NULL);
    assert(combo != 0);                            /* a hotkey needs at least one button */
    if (h->count >= HOTKEY_MAX)
        return -1;
    int32_t i = (int32_t)h->count++;
    h->hotkeys[i].combo = combo;
    h->hotkeys[i].action = action;
    h->hotkeys[i].param = param;
    return i;
}

buttons_t hotkeys_step(hotkeys_t *h, buttons_t pressed, hotkey_fire_t *fired,
                       uint32_t cap, uint32_t *nfired)
{
    assert(h != NULL);
    assert(nfired != NULL);

    buttons_t out = pressed;
    uint32_t n = 0;
    for (uint32_t i = 0; i < h->count; i++) {
        buttons_t combo = h->hotkeys[i].combo;
        if ((pressed & combo) == combo) {          /* combo fully held */
            out &= ~combo;                          /* mask it from the output */
            if ((h->prev & combo) != combo) {       /* edge: not fully held last tick */
                if (fired != NULL && n < cap) {
                    fired[n].action = h->hotkeys[i].action;
                    fired[n].param = h->hotkeys[i].param;
                    n++;
                }
            }
        }
    }
    h->prev = pressed;
    *nfired = n;
    /* negative space: masking only removes buttons, it never invents one */
    assert((out & ~pressed) == 0);
    return out;
}
