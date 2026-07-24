#include "core_bridge.h"

/* the vendored core is C; give its declarations C linkage so they bind to socd.c */
extern "C" {
#include "core/socd.h"
}

namespace {

/* GP2040 SOCDMode -> core socd_mode_t. Correspondence confirmed by reading both
 * cleaners and proven by test/socd_equiv.c. */
socd_mode_t mapMode(SOCDMode m) {
    switch (m) {
        case SOCD_MODE_UP_PRIORITY:           return SOCD_UP_PRIORITY;
        case SOCD_MODE_SECOND_INPUT_PRIORITY: return SOCD_LAST_WIN;
        case SOCD_MODE_FIRST_INPUT_PRIORITY:  return SOCD_FIRST_WIN;
        case SOCD_MODE_BYPASS:                return SOCD_BYPASS;
        case SOCD_MODE_NEUTRAL:
        default:                              return SOCD_NEUTRAL;
    }
}

/* Session-lifetime state -- the analogue of runSOCDCleaner's static lastUD/lastLR. */
socd_t   g_socd;
SOCDMode g_mode   = (SOCDMode)-1;
bool     g_inited = false;

} // namespace

namespace CoreInput {

uint8_t cleanDpad(SOCDMode mode, uint8_t dpad) {
    if (!g_inited) {
        socd_init(&g_socd, mapMode(mode));
        g_inited = true;
    } else if (mode != g_mode) {
        g_socd.mode = mapMode(mode);   /* re-seat mode, keep press-order history */
    }
    g_mode = mode;

    /* dpad bits UP..RIGHT (0..3) == BTN_UP..RIGHT; only the four dirs are meaningful */
    buttons_t cleaned = socd_clean(&g_socd, (buttons_t)(dpad & 0x0Fu));
    return (uint8_t)(cleaned & 0x0Fu);
}

} // namespace CoreInput
