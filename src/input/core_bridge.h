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
    /*
     * How the sync window should behave. Grouped into a struct rather than eight positional
     * arguments, and built once per loop by GP2040::syncGpioGetAll from GamepadOptions.
     *
     * syncedMask / attackMask are two DIFFERENT questions and must not be conflated:
     *   syncedMask  which pins the window may DELAY. Excluding directions keeps a lever sweep
     *               (1-3 ms in a zone) out of a stage that would drop or fuse it, and stops
     *               every movement input paying the window for nothing.
     *   attackMask  which pins mean "a chord is forming", for eagerCommit. Without it, eager
     *               commit fires on direction+button and disables the window for anything
     *               that moves.
     * 0 means "all pins" for either, so a zeroed policy is the classic behaviour.
     */
    struct SyncPolicy {
        uint32_t window;          // ms; 0 is not valid here, the caller clamps
        bool     releaseDebounce;
        uint32_t syncedMask;      // 0 = every pin is subject to the window
        uint32_t attackMask;      // 0 = every pin counts toward eagerCommit
        uint32_t eagerCommit;     // 0 = ride out the window; N>=2 = commit at N pending
        bool     preserveWidth;   // delay each release by the delay its own press incurred
    };

    uint32_t syncGpio(uint32_t rawButtons, const SyncPolicy& policy, uint32_t now);

    /*
     * Turbo (auto-fire) via the DST-proven core turbo_step -- REPLACES the addon's ad-hoc software
     * flicker (getMicro poll + LOOP_OFFSET fudge) with the fuzzed reference. `buttons` is the
     * post-SOCD pressed mask, `turboMask` the bits with turbo enabled, `shotCount` shots/sec
     * (2..30), `now` millis(). Returns the mask with turbo'd buttons pulsed off in their OFF phase.
     * Phase is absolute-time (ms ticks): the rising edge is always ON (no dropped first shot) and it
     * self-resets per button on release -- so the addon no longer tracks flicker state itself.
     */
    uint16_t turbo(uint16_t buttons, uint16_t turboMask, uint8_t shotCount, uint32_t now);
}
