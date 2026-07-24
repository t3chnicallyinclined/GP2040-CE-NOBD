#ifndef NOBD_CORE_FRAME_H
#define NOBD_CORE_FRAME_H
#include <stdint.h>
#include "buttons.h"

/*
 * input_frame_t -- the published input state: the cleaned digital button mask plus a
 * few analog/Hall axes. This is the DATA CONTRACT between the input pipeline (which
 * produces it), the buffer (which publishes it across cores, buffer.h), and the
 * reflectors (which format it per console, under firmware/usb). Kept tiny so the
 * cross-core publish can be a single atomic word where possible.
 *
 * Reflectors currently use the digital `buttons` + neutral analog sticks (matching
 * the Python reference); mapping `axis` to console triggers/sticks is a future joint
 * step (change both the C and the Python spec together).
 */
#define INPUT_FRAME_AXES 6u    /* analog/Hall axes (0..255; 0x80 = center for sticks) */

typedef struct {
    buttons_t buttons;
    uint8_t   axis[INPUT_FRAME_AXES];
} input_frame_t;

#endif /* NOBD_CORE_FRAME_H */
