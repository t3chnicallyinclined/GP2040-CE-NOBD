#include <assert.h>
#include <stddef.h>   /* NULL */
#include "socd.h"

/*
 * One SOCD axis (a pair of opposing directions). Returns the bits to KEEP for the
 * axis and updates *win with the current winner. A faithful port of the per-axis
 * loop in ../../input/socd.py -- keep the two in lockstep.
 */
static buttons_t socd_axis(socd_mode_t mode, buttons_t pressed, buttons_t prev,
                           buttons_t bit_a, buttons_t bit_b, buttons_t *win)
{
    int ca = (pressed & bit_a) != 0, cb = (pressed & bit_b) != 0;
    int pa = (prev & bit_a) != 0,    pb = (prev & bit_b) != 0;
    buttons_t keep = 0;

    if (ca && cb) {                                  /* SOCD conflict on this axis */
        switch (mode) {
        case SOCD_BYPASS:
            keep = bit_a | bit_b;
            break;
        case SOCD_NEUTRAL:
            keep = 0;                                /* neither survives            */
            break;
        case SOCD_UP_PRIORITY:
            keep = (bit_a == BTN_UP) ? bit_a : 0;    /* Up wins; horizontal -> 0    */
            break;
        case SOCD_LAST_WIN:
            if (ca && !pa && cb && !pb) *win = 0;     /* both same frame -> no winner (neutral) */
            else if (ca && !pa)         *win = bit_a; /* newest single press takes the axis */
            else if (cb && !pb)         *win = bit_b;
            keep = (*win == bit_a ? bit_a : 0) | (*win == bit_b ? bit_b : 0);
            break;
        case SOCD_FIRST_WIN:
            /* first press to stand alone wins and holds; a same-frame tie from neutral
             * has no "first" -> stays neutral (win==0) until one direction stands alone */
            keep = (*win == bit_a ? bit_a : 0) | (*win == bit_b ? bit_b : 0);
            break;
        }
    } else if (ca) {
        keep = bit_a; *win = bit_a;
    } else if (cb) {
        keep = bit_b; *win = bit_b;
    } else {
        *win = 0;                                    /* axis released -> no winner  */
    }
    return keep;
}

void socd_init(socd_t *s, socd_mode_t mode)
{
    assert(s != NULL);
    assert(mode <= SOCD_BYPASS);      /* enum is unsigned; SOCD_NEUTRAL==0 is the floor */
    s->mode = mode;
    s->prev = 0;
    s->win_v = 0;
    s->win_h = 0;
}

buttons_t socd_clean(socd_t *s, buttons_t pressed)
{
    assert(s != NULL);

    buttons_t keep_v = socd_axis(s->mode, pressed, s->prev, BTN_UP, BTN_DOWN, &s->win_v);
    buttons_t keep_h = socd_axis(s->mode, pressed, s->prev, BTN_LEFT, BTN_RIGHT, &s->win_h);

    /* non-direction buttons pass through untouched; only the four dirs are gated */
    buttons_t out = (pressed & ~BTN_DIRS) | keep_v | keep_h;
    s->prev = pressed;

    /* negative space: after cleaning, opposing directions never both survive
     * (bypass is the one mode that deliberately lets them through). */
    assert(s->mode == SOCD_BYPASS || (out & (BTN_UP | BTN_DOWN)) != (BTN_UP | BTN_DOWN));
    assert(s->mode == SOCD_BYPASS || (out & (BTN_LEFT | BTN_RIGHT)) != (BTN_LEFT | BTN_RIGHT));
    return out;
}
