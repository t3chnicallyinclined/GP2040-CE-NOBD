#ifndef NOBD_CORE_BUTTONS_H
#define NOBD_CORE_BUTTONS_H
#include <stdint.h>

/*
 * Canonical logical-button bitmask shared by every core module.
 *
 * One press = one bit. The portable core operates on this u32 mask (static,
 * branch-light, trivially copied across the HSEM-guarded shared buffer) rather
 * than on names or sets. The name <-> bit mapping mirrors the reflector report
 * formatters under firmware/usb; those formatters are the only place bits become
 * console-specific wire values.
 *
 * Only the four DIRECTIONS carry fixed meaning to the input pipeline (SOCD acts
 * on them). The action bits are OPAQUE to the pipeline -- it passes them through
 * untouched -- so their exact positions can firm up as the reflector map settles
 * without touching socd/turbo/macros logic.
 */
typedef uint32_t buttons_t;

#define BUTTONS_BITS 32u   /* buttons_t is a 32-bit mask (per-bit state tables size to this) */

#define BTN_UP    ((buttons_t)1u << 0)
#define BTN_DOWN  ((buttons_t)1u << 1)
#define BTN_LEFT  ((buttons_t)1u << 2)
#define BTN_RIGHT ((buttons_t)1u << 3)
#define BTN_DIRS  (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)

/* Fight-stick action buttons -- the canonical layout the reflectors (under
 * firmware/usb) map FROM. Opaque to the input pipeline (it passes them through); only
 * the reflectors assign console-specific meaning. Positions match the name<->bit map
 * in the reflector bridges. */
#define BTN_P1     ((buttons_t)1u << 4)     /* punches: light .. heavy .. 4th */
#define BTN_P2     ((buttons_t)1u << 5)
#define BTN_P3     ((buttons_t)1u << 6)
#define BTN_P4     ((buttons_t)1u << 7)
#define BTN_K1     ((buttons_t)1u << 8)     /* kicks */
#define BTN_K2     ((buttons_t)1u << 9)
#define BTN_K3     ((buttons_t)1u << 10)
#define BTN_K4     ((buttons_t)1u << 11)
#define BTN_START  ((buttons_t)1u << 12)
#define BTN_SELECT ((buttons_t)1u << 13)
#define BTN_L3     ((buttons_t)1u << 14)    /* stick clicks */
#define BTN_R3     ((buttons_t)1u << 15)

/* Generic aliases (B1..B4 == P1..P4) kept for the core-module tests. */
#define BTN_B1 BTN_P1
#define BTN_B2 BTN_P2
#define BTN_B3 BTN_P3
#define BTN_B4 BTN_P4

#endif /* NOBD_CORE_BUTTONS_H */
