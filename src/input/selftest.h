/*
 * selftest -- programmatic input stimulus (TE_SELFTEST builds only, never shipped).
 *
 * The firmware drives its OWN button pins on a fixed, repeating schedule, so the whole
 * edge -> front stage -> ISR report -> USB path is exercised with NO switch, NO wiring and
 * NO finger in the loop. A press is "output-enable, driving LOW" (exactly what a closed
 * switch does to a pulled-up input); a release is "back to input" (the pull-up restores
 * it). The pin is never driven HIGH, so a physically connected switch can never be shorted.
 * The RP2040's edge IRQ watches the pad input, so the doorbell sees these edges exactly as
 * it sees a real press.
 *
 * The schedule is the contract the host script (tools/selftest/selftest.py) checks against:
 * every press and release must appear, in order, with the right width, and nothing may stay
 * down. See the table in selftest.cpp.
 */
#pragma once
#include <stdint.h>

namespace SelfTest {
    // Arm the stimulus: it starts SELFTEST_START_DELAY_MS after this call (gives USB time to
    // enumerate) and repeats forever. Pins are taken from the live gamepad mapping (B1, B2, UP).
    void start();

    // Diagnostics: how many schedule steps have fired (0 = the stimulus never started).
    uint32_t stepCount();
}
