#include "core_bridge.h"
#include "pico.h"   // __not_in_flash_func -- RAM-pin the hot bridge (no XIP jitter)

/* the vendored core is C; give its declarations C linkage so they bind to socd.c */
extern "C" {
#include "core/socd.h"
#include "core/sync_window.h"
#include "core/turbo.h"
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

uint8_t __not_in_flash_func(cleanDpad)(SOCDMode mode, uint8_t dpad) {
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

uint32_t __not_in_flash_func(syncGpio)(uint32_t rawButtons, uint32_t window, bool releaseDebounce, uint32_t now) {
    static sync_window_t sw;
    static bool sw_inited = false;
    uint32_t w = window < 1u ? 1u : (window > SYNC_WINDOW_MAX ? SYNC_WINDOW_MAX : window);
    if (!sw_inited) { sync_window_init(&sw, w, releaseDebounce); sw_inited = true; }
    sw.window = w;                       /* dynamic: the incumbent re-read nobdSyncDelay each call */
    sw.release_debounce = releaseDebounce;
    return (uint32_t)sync_window_step(&sw, now, (buttons_t)rawButtons);
}

uint16_t __not_in_flash_func(turbo)(uint16_t buttons, uint16_t turboMask, uint8_t shotCount, uint32_t now) {
    static turbo_t t;
    static bool inited = false;
    /* ms ticks: half-period = round(500 / shotCount) ms per phase, so period = 1/shotCount s. Both
     * phases equal (50% duty). shotCount is 2..30 -> half is 17..250, inside the core's [1,255] cap. */
    uint32_t sc   = shotCount < 2u ? 2u : (shotCount > 30u ? 30u : shotCount);
    uint32_t half = (500u + sc / 2u) / sc;
    if (half < 1u)                half = 1u;
    if (half > TURBO_MAX_TICKS)   half = TURBO_MAX_TICKS;
    if (!inited) { turbo_init(&t, (buttons_t)turboMask, half, half); inited = true; }
    /* Re-seat config in place each call (preserve per-button phase in since[]/tracking), exactly
     * like cleanDpad re-seats the SOCD mode -- absolute-time phase re-lands correctly on a change. */
    t.buttons   = (buttons_t)turboMask;
    t.on_ticks  = half;
    t.off_ticks = half;
    t.period    = half + half;
    return (uint16_t)turbo_step(&t, now, (buttons_t)buttons);
}

} // namespace CoreInput
