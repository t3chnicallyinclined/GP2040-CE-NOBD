#if TE_SELFTEST
#include "selftest.h"
#include "pico.h"
#include "pico/time.h"
#include "hardware/structs/sio.h"
#include "storagemanager.h"

// ---- THE SCHEDULE (ms offset within a 1000 ms cycle; A = B1, B = B2, U = UP) -------------------
//
//    0  A down     40  A up      lone tap, 40 ms. nobd-25 held this until the NEXT input.
//  200  A down    240  A up      double tap, 60 ms gap. nobd-25 swallowed the second press.
//  300  A down    340  A up
//  500  A down    502  B down    chord 2 ms apart: must land in ONE report with sync on.
//  560  A up + B up
//  700  U down    740  U up      direction: never delayed by the window (anchor for the host).
//  800  A down    810  A up      10 ms tap: longer than a 5 ms window, must register.
//  900  A down    903  A up      3 ms tap: shorter than the window -> dropped BY DESIGN (sync on).
//
// Keep tools/selftest/selftest.py EXPECTED in lockstep with this table.
namespace {

constexpr uint32_t SELFTEST_START_DELAY_MS = 3000;

enum : uint8_t { A = 1u, B = 2u, U = 4u };
struct Step { uint16_t at_ms; uint8_t down; uint8_t up; };
constexpr Step SCHEDULE[] = {
    {   0, A, 0 }, {  40, 0, A },
    { 200, A, 0 }, { 240, 0, A },
    { 300, A, 0 }, { 340, 0, A },
    { 500, A, 0 }, { 502, B, 0 }, { 560, 0, A | B },
    { 700, U, 0 }, { 740, 0, U },
    { 800, A, 0 }, { 810, 0, A },
    { 900, A, 0 }, { 903, 0, A },
};
constexpr uint32_t CYCLE_MS = 1000;
constexpr unsigned NSTEPS = sizeof(SCHEDULE) / sizeof(SCHEDULE[0]);

uint32_t s_pins[3] = {0, 0, 0};          // GPIO masks for A, B, U
unsigned s_step = 0;
volatile uint32_t s_steps = 0;            // diagnostics: total steps fired
alarm_id_t s_alarm = 0;

inline uint32_t pinsOf(uint8_t bits) {
    return (bits & A ? s_pins[0] : 0u) | (bits & B ? s_pins[1] : 0u) | (bits & U ? s_pins[2] : 0u);
}

// Runs in the alarm-pool IRQ. Raw SIO register writes: no SDK call, no flash, microsecond-exact.
int64_t __not_in_flash_func(stepCb)(alarm_id_t, void*) {
    const Step& st = SCHEDULE[s_step];
    s_steps++;
    if (st.up)   sio_hw->gpio_oe_clr = pinsOf(st.up);     // release: back to input, pull-up wins
    if (st.down) sio_hw->gpio_oe_set = pinsOf(st.down);   // press: drive the (pre-cleared) LOW
    const unsigned next = (s_step + 1u) % NSTEPS;
    const uint32_t delta = (next == 0u) ? (CYCLE_MS - st.at_ms + SCHEDULE[0].at_ms)
                                        : (uint32_t)(SCHEDULE[next].at_ms - st.at_ms);
    s_step = next;
    // Negative = relative to the SCHEDULED fire time, not to now: the cycle never drifts.
    return -(int64_t)delta * 1000;
}

int64_t armCb(alarm_id_t, void*) {
    Gamepad* gp = Storage::getInstance().GetGamepad();
    s_pins[0] = gp->mapButtonB1 ? gp->mapButtonB1->pinMask : 0u;
    s_pins[1] = gp->mapButtonB2 ? gp->mapButtonB2->pinMask : 0u;
    s_pins[2] = gp->mapDpadUp   ? gp->mapDpadUp->pinMask   : 0u;
    const uint32_t all = s_pins[0] | s_pins[1] | s_pins[2];
    sio_hw->gpio_clr = all;              // output latch LOW now; OE alone will press/release
    s_step = 0;
    s_alarm = add_alarm_in_us(1000, &stepCb, nullptr, true);
    return 0;
}

} // namespace

namespace SelfTest {
void start() {
    add_alarm_in_ms(SELFTEST_START_DELAY_MS, &armCb, nullptr, true);
}
uint32_t stepCount() { return s_steps; }
}
#endif // TE_SELFTEST
