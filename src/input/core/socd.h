#ifndef NOBD_CORE_SOCD_H
#define NOBD_CORE_SOCD_H
#include "buttons.h"

/*
 * SOCD cleaning -- resolves Simultaneous Opposite Cardinal Directions (Left+Right,
 * Up+Down) before the buffer. Portable core module: this is the C the chip runs,
 * and it compiles unchanged for the host so the DST/VOPR can fuzz it (test what
 * ships). It mirrors the reference logic + tests in ../../input/socd.py.
 *
 * On-chip (golden rule -- offload from the CPU): SOCD is irreducible CPU logic (the
 * PIOC's 2 pins can't do it), but it runs on the deterministic V3F core in a
 * zero-overhead VTF+HPE interrupt -- fired on an input-change EXTI, not a poll loop --
 * and the mask ops map to the RV32 B bit-manip extension.
 *
 * TigerStyle: static state (no allocation), bounded (constant work per call), and
 * the negative-space invariant (never leak both opposing directions) is asserted
 * inside socd_clean().
 */
typedef enum {
    SOCD_NEUTRAL = 0,   /* both opposing directions cancel                        */
    SOCD_UP_PRIORITY,   /* Up+Down -> Up; Left+Right -> neutral                   */
    SOCD_LAST_WIN,      /* most recently pressed direction wins (stateful)        */
    SOCD_FIRST_WIN,     /* first pressed direction holds (stateful)               */
    SOCD_BYPASS         /* no cleaning -- raw dual-press passes through            */
} socd_mode_t;

typedef struct {
    socd_mode_t mode;
    buttons_t   prev;   /* previous raw mask, for press-order edge detection      */
    buttons_t   win_v;  /* winning vertical direction bit (BTN_UP/BTN_DOWN/0)     */
    buttons_t   win_h;  /* winning horizontal direction bit (BTN_LEFT/BTN_RIGHT/0)*/
} socd_t;

void      socd_init(socd_t *s, socd_mode_t mode);
buttons_t socd_clean(socd_t *s, buttons_t pressed);

#endif /* NOBD_CORE_SOCD_H */
