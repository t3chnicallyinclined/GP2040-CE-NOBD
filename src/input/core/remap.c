#include <assert.h>
#include <stddef.h>   /* NULL */
#include "remap.h"

/* A faithful port of ../../input/remap.py (a pure physical->logical mapping). */

void remap_init(remap_t *r)
{
    assert(r != NULL);
    for (uint32_t i = 0; i < BUTTONS_BITS; i++)
        r->logical_of[i] = (buttons_t)1u << i;   /* identity: pin i -> bit i */
}

void remap_set(remap_t *r, uint32_t pin, buttons_t logical)
{
    assert(r != NULL);
    assert(pin < BUTTONS_BITS);
    r->logical_of[pin] = logical;
}

buttons_t remap_apply(const remap_t *r, buttons_t physical)
{
    assert(r != NULL);
    buttons_t out = 0;
    for (uint32_t i = 0; i < BUTTONS_BITS; i++)
        if (physical & ((buttons_t)1u << i))
            out |= r->logical_of[i];
    /* negative space: no physical bits set -> nothing produced */
    assert(physical != 0 || out == 0);
    return out;
}
