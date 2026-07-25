/*
 * core_bridge -- the seam between GP2040's GamepadState and the vendored, DST-proven
 * input core (src/input/core/). It is the ONE place GP2040 types become core `buttons_t`
 * and back, so the console reflectors never see anything but cleaned state.
 *
 * Phase 1 (here): SOCD only. cleanDpad() replaces the incumbent runSOCDCleaner() with the
 * core's socd_clean(), proven behavior-equivalent by test/socd_equiv.c (exhaustive over
 * every length-4 dpad sequence + a 2M-step walk, all modes). GP2040's dpad bits
 * (UP..RIGHT == bits 0..3) already equal the core's BTN_UP..RIGHT, so no bit shuffle.
 *
 * Later phases grow this seam into the full input_pipeline_step + buffer_publish path
 * (sync window / turbo / macros), each stage differentially validated before it lands.
 */
#pragma once
#include <stdint.h>
#include "enums.pb.h"   /* SOCDMode */

namespace CoreInput {
    /*
     * SOCD-clean a 4-bit GP2040 dpad mask through the core cleaner. Keeps persistent
     * per-axis state across calls (exactly like the incumbent's function-statics), so
     * last/first-win press order is honored over the whole session. Re-seats the mode
     * in place when it changes (history preserved, matching the incumbent).
     */
    uint8_t cleanDpad(SOCDMode mode, uint8_t dpad);

    /*
     * NOBD sync window (co-registration) via the DST-proven core sync_window_step, which
     * REPLACES debounce in the input-conditioning slot (one or the other). rawButtons is the
     * pressed-button GPIO mask; window is nobdSyncDelay in ms (clamped 1..500); now is
     * millis() (monotonic). Returns the co-registered button mask.
     */
    uint32_t syncGpio(uint32_t rawButtons, uint32_t window, bool releaseDebounce, uint32_t now);
}
