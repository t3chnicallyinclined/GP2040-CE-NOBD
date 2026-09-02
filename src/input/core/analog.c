#include <assert.h>
#include <stddef.h>   /* NULL */
#include "analog.h"

/*
 * A faithful port of ../../input/analog.py. `ref` is the local extremum: the minimum
 * while released (so a down-move of press_sens presses), the maximum while pressed
 * (so an up-move of release_sens releases). Keep the two in lockstep; input/dst.py
 * tier I13 proves they agree under fuzzing.
 */

void analog_init(analog_t *a, const analog_config_t *cfg)
{
    assert(a != NULL);
    assert(cfg != NULL);
    assert(cfg->mode == ANALOG_FIXED || cfg->mode == ANALOG_RAPID);
    assert(cfg->press_sens >= 1u && cfg->release_sens >= 1u);
    a->cfg = *cfg;
    a->pressed = false;
    a->ref = 0;
}

bool analog_step(analog_t *a, uint8_t value)
{
    assert(a != NULL);
    const analog_config_t *c = &a->cfg;

    if (c->mode == ANALOG_FIXED) {
        if (!a->pressed && value >= c->actuation)
            a->pressed = true;                                  /* cross the actuation point */
        else if (a->pressed && (int)value <= (int)c->actuation - (int)c->hysteresis)
            a->pressed = false;                                 /* fall below the release band */
    } else {                                                    /* rapid trigger */
        if (value < c->floor) {
            a->pressed = false;
            a->ref = value;
        } else if (!a->pressed) {
            if (value < a->ref)
                a->ref = value;                                 /* track the local minimum */
            if ((int)value - (int)a->ref >= (int)c->press_sens) {
                a->pressed = true;
                a->ref = value;                                 /* switch to tracking the maximum */
            }
        } else {
            if (value > a->ref)
                a->ref = value;                                 /* track the local maximum */
            if ((int)a->ref - (int)value >= (int)c->release_sens) {
                a->pressed = false;
                a->ref = value;                                 /* switch to tracking the minimum */
            }
        }
    }

    /* negative space: rapid trigger is never actuated below the floor */
    assert(!(a->pressed && c->mode == ANALOG_RAPID && value < c->floor));
    return a->pressed;
}
