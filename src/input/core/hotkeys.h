#ifndef NOBD_CORE_HOTKEYS_H
#define NOBD_CORE_HOTKEYS_H
#include "buttons.h"

/*
 * Hotkeys -- button COMBOS that fire config actions instead of reaching the game.
 * A hotkey is a set of buttons; it FIRES on the tick the combo becomes fully pressed
 * (an edge), so holding it fires once. While held, the combo buttons are MASKED from
 * the output (a command, not gameplay). Runs late in the pipeline on resolved logical
 * buttons. Mirrors ../../input/hotkeys.py (fuzzed differentially by input/dst.py I17).
 *
 * Actions are OPAQUE codes here; applying them (profile switch, SOCD cycle, ...) is the
 * config/profiles layer's job (see profiles.h), not this module's.
 *
 * TigerStyle: static table (no allocation), bounded (one pass over the hotkeys),
 * asserts + the never-fabricate negative space, named cap.
 */
#define HOTKEY_MAX 16u    /* max hotkeys (GP2040 also caps at 16) */

typedef enum {
    HOTKEY_NONE = 0,
    HOTKEY_PROFILE_NEXT, HOTKEY_PROFILE_PREV, HOTKEY_PROFILE_SET,
    HOTKEY_SOCD_CYCLE, HOTKEY_SOCD_SET, HOTKEY_SYNC_TOGGLE,
    HOTKEY_TURBO_UP, HOTKEY_TURBO_DOWN, HOTKEY_DPAD_MODE, HOTKEY_FOURWAY_TOGGLE,
    HOTKEY_REBOOT_BOOTLOADER, HOTKEY_WEBCONFIG
} hotkey_action_t;

typedef struct {
    buttons_t       combo;    /* the button set that triggers this hotkey (>= 1 button) */
    hotkey_action_t action;
    uint8_t         param;    /* e.g. profile index / SOCD mode */
} hotkey_t;

typedef struct {
    hotkey_t  hotkeys[HOTKEY_MAX];
    uint32_t  count;
    buttons_t prev;           /* last tick's pressed set (edge detection) */
} hotkeys_t;

typedef struct { hotkey_action_t action; uint8_t param; } hotkey_fire_t;

void      hotkeys_init(hotkeys_t *h);
int32_t   hotkeys_add(hotkeys_t *h, buttons_t combo, hotkey_action_t action, uint8_t param);
/* Returns the masked output (combo buttons removed). Appends fired actions -- combos
 * that just completed this tick, in add order -- into fired[0..cap), writing the count
 * to *nfired. */
buttons_t hotkeys_step(hotkeys_t *h, buttons_t pressed, hotkey_fire_t *fired,
                       uint32_t cap, uint32_t *nfired);

#endif /* NOBD_CORE_HOTKEYS_H */
