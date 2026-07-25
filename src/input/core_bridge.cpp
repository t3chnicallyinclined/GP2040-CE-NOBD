#include "core_bridge.h"

/* the vendored core is C; give its declarations C linkage so they bind to socd.c */
extern "C" {
#include "core/socd.h"
#include "core/sync_window.h"
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

uint32_t syncGpio(uint32_t rawButtons, uint32_t window, bool releaseDebounce, uint32_t now) {
    static sync_window_t sw;
    static bool sw_inited = false;
    uint32_t w = window < 1u ? 1u : (window > SYNC_WINDOW_MAX ? SYNC_WINDOW_MAX : window);
    if (!sw_inited) { sync_window_init(&sw, w, releaseDebounce); sw_inited = true; }
    sw.window = w;                       /* dynamic: the incumbent re-read nobdSyncDelay each call */
    sw.release_debounce = releaseDebounce;
    return (uint32_t)sync_window_step(&sw, now, (buttons_t)rawButtons);
}

} // namespace CoreInput
