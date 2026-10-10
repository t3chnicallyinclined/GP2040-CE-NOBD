#include "gpio_doorbell.h"
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"    // save_and_disable_interrupts / restore_interrupts
#include "hardware/timer.h"   // hardware_alarm_* + time
#include "hardware/structs/io_bank0.h"
#include "pico/time.h"        // to_ms_since_boot / get_absolute_time / from_us_since_boot
#include "latency_probe.h"

// The DST-proven sync-window core is C; give it C linkage (binds to sync_window.c).
extern "C" {
#include "core/sync_window.h"
}

namespace {

volatile uint32_t g_edges   = 0;
volatile bool     g_changed = false;

// The rung-2 fast path: a driver-supplied builder run in the edge/timer ISRs, handed the
// sync-committed pressed mask. Function pointer (single slot). Read from the ISRs, written once.
void (* volatile g_fastpath)(uint32_t) = nullptr;

// ---- Stage 4 state: the doorbell owns the sync window --------------------------------------
uint32_t          s_buttonMask = 0;        // union of button pins (mask the raw read to these)
sync_window_t     s_sync;                  // THE single sync state (was CoreInput::syncGpio's)
bool              s_syncInited  = false;
volatile bool     s_syncActive  = false;   // false => pins pass straight through (rung-2 path)
volatile uint32_t s_committed   = 0;       // latest committed pressed mask (loop reads this)
int               s_alarm       = -1;      // claimed hardware alarm for the commit deadline
bool              s_alarmInited = false;
bool              s_irqInstalled = false;  // exclusive IO_IRQ_BANK0 handler installed once
volatile bool     s_debounceActive = false;                    // Stage 3: leading-edge debounce
uint32_t          s_debounceDelay  = 0;                        // lockout ms
uint32_t          s_debounceTime[NUM_BANK0_GPIOS] = {0};       // per-pin last accepted-edge ms

// ---- The deadline ledger: every time-driven event the front stage owes, as an ABSOLUTE
// microsecond target recorded ONCE, on the edge that created it -----------------------------
//
// Why a ledger and not "re-arm the alarm with (deadline_ms - now_ms) every step": the core keeps
// deadlines in whole ms, and make_timeout_time_ms() adds that count to the TRUE us clock at the
// instant of the call. The FIRST arm, on the press edge, is exact (t_edge + W*1000). Every later
// re-arm -- a bounce edge, the second button of a chord, an ordinary loop pass -- restated the
// same ms deadline as deadline_ms + frac(t_call) and MOVED the commit anywhere in (W-1, W+1) ms.
// Two milliseconds of commit spread on a 1 ms transport, precisely on the chords sync exists for.
// Host sim (gp2040-te tools/sim_sync_arm.py, 57,200 cases, W=5): 4.009..5.999 ms re-arming each
// step; 5.000 to the microsecond when the deadline is recorded once and never restated.
//
// Why EVERY event and not just the press window: the preserve-width release (rel_due per bit),
// the release-debounce window and the debounce lockout expiry had NO alarm at all. They relied
// on the loop polling committed(), and the A4b idle-skip stops that poll the moment the fast path
// is armed -- so a lone release landed only on the NEXT edge of any pin, and a same-button
// re-press cancelled it outright (the tap was swallowed). Every deadline now wakes the CPU.
//
// Records are keyed by the ms deadline the core holds (press, release-window) or by bit (per-bit
// release debt / debounce lockout), so a step that re-derives the same deadline recognises it and
// leaves the us target alone. The hardware alarm is armed at the NEAREST recorded target and
// re-armed only when that nearest target changes. Touched only inside the critical section.
struct Ledger {
    bool     pressValid;   uint32_t pressMs;   uint64_t pressUs;    // s_sync.open / deadline
    bool     relValid;     uint32_t relMs;     uint64_t relUs;      // s_sync.release_open / release_deadline
    uint32_t bitsValid;                                             // per-bit records live for these bits
    uint64_t bitUs[NUM_BANK0_GPIOS];                                // rel_due (sync) or lockout expiry (debounce)
};
Ledger        s_ledger = {};
volatile bool s_alarmArmed = false;        // the hardware alarm is set for s_alarmUs
uint64_t      s_alarmUs    = 0;

inline void ledgerClear() {
    s_ledger.pressValid = false;
    s_ledger.relValid   = false;
    s_ledger.bitsValid  = 0;
}

// Point the one-pulse alarm at the nearest recorded target. Inside the critical section.
inline void __not_in_flash_func(armNearest)() {
    if (s_alarm < 0) return;
    bool any = false;
    uint64_t best = 0;
    if (s_ledger.pressValid) { best = s_ledger.pressUs; any = true; }
    if (s_ledger.relValid && (!any || s_ledger.relUs < best)) { best = s_ledger.relUs; any = true; }
    uint32_t bits = s_ledger.bitsValid;
    while (bits) {
        const uint b = (uint)__builtin_ctz(bits);
        if (!any || s_ledger.bitUs[b] < best) { best = s_ledger.bitUs[b]; any = true; }
        bits &= bits - 1u;
    }
    if (!any) { s_alarmArmed = false; return; }          // nothing owed; a stale alarm firing is a harmless re-step
    if (s_alarmArmed && best == s_alarmUs) return;        // already armed for exactly this instant
    s_alarmUs = best;
    // A target already in the past (only possible if we were held off for a whole ms) is one
    // the SDK will NEVER fire -- set_target returns true and leaves no callback pending, so a
    // forced IRQ would be swallowed too. Re-aim a hair ahead of now until it takes: the callback
    // then re-steps and commits immediately, instead of trusting an alarm that is never coming
    // (the stuck-input failure mode). Bounded: each retry is 50 us ahead of a fresh clock read.
    for (int tries = 0; tries < 8; tries++) {
        if (!hardware_alarm_set_target((uint)s_alarm, from_us_since_boot(best))) {
            s_alarmArmed = true;
            return;
        }
        best = time_us_64() + 50u;
    }
    s_alarmArmed = false;   // pathological: leave it to the loop poll / next edge
}

// Step the input front stage with the current pins, update s_committed, and arm the one-pulse
// alarm at the nearest deadline the stage still owes. Returns true if committed changed.
// RAM-pinned. When sync is OFF this is the validated ~1us straight-through path (no now/CS/alarm).
bool __not_in_flash_func(stepSync)() {
    const uint32_t raw = (~gpio_get_all()) & s_buttonMask;   // active-low -> pressed=1, masked
    uint32_t c;
    bool changed;

    if (s_syncActive) {
        const absolute_time_t t  = get_absolute_time();   // ONE clock read: ms for the core, us for the ledger
        const uint64_t tus       = to_us_since_boot(t);
        const uint32_t now       = (uint32_t)(tus / 1000u);
        // s_sync is touched by the edge ISR, the alarm ISR, AND the loop -- a short critical
        // section makes the step + its commit atomic across all three (single owner, no tear).
        const uint32_t save = save_and_disable_interrupts();
        c = (uint32_t)sync_window_step(&s_sync, now, (buttons_t)raw);
        changed = (c != s_committed);
        s_committed = c;

        // Record each owed deadline ONCE (see the ledger note above), then arm the nearest.
        if (s_sync.open) {
            if (!s_ledger.pressValid || s_ledger.pressMs != s_sync.deadline) {
                s_ledger.pressValid = true;
                s_ledger.pressMs    = s_sync.deadline;
                s_ledger.pressUs    = tus + (uint64_t)(s_sync.deadline - now) * 1000u;   // exact: this IS the edge
            }
        } else {
            s_ledger.pressValid = false;
        }
        if (s_sync.release_open) {
            if (!s_ledger.relValid || s_ledger.relMs != s_sync.release_deadline) {
                s_ledger.relValid = true;
                s_ledger.relMs    = s_sync.release_deadline;
                s_ledger.relUs    = tus + (uint64_t)(s_sync.release_deadline - now) * 1000u;
            }
        } else {
            s_ledger.relValid = false;
        }
        {   // preserve-width: a bit entering `releasing` owes exactly its own delay from THIS edge
            uint32_t fresh = (uint32_t)s_sync.releasing & ~s_ledger.bitsValid;
            while (fresh) {
                const uint b = (uint)__builtin_ctz(fresh);
                s_ledger.bitUs[b] = tus + (uint64_t)(s_sync.rel_due[b] - now) * 1000u;
                fresh &= fresh - 1u;
            }
            s_ledger.bitsValid = (uint32_t)s_sync.releasing;   // a re-press cancels the debt: record dropped
        }
        armNearest();
        restore_interrupts(save);
    } else if (s_debounceActive) {
        // Leading-edge per-pin debounce: accept a pin's change immediately, then lock that pin out
        // for delayMs (identical algorithm to GP2040's debounceGpioGetAll, but run on the edge).
        // Only walks pins that actually changed. A change that settles DURING a lockout is owed a
        // wake at the lockout's expiry (ledger), so it lands on time with the CPU asleep -- it
        // used to rely on the loop poll, which the idle-skip removes.
        const absolute_time_t t  = get_absolute_time();
        const uint64_t tus       = to_us_since_boot(t);
        const uint32_t now       = (uint32_t)(tus / 1000u);
        const uint32_t save = save_and_disable_interrupts();
        const uint32_t before = s_committed;
        uint32_t chg = (raw ^ before) & s_buttonMask;
        while (chg) {
            const uint pin = (uint)__builtin_ctz(chg);
            if ((uint32_t)(now - s_debounceTime[pin]) > s_debounceDelay) {
                s_committed ^= (1u << pin);
                s_debounceTime[pin] = now;
            }
            chg &= (chg - 1);
        }
        c = s_committed;
        changed = (c != before);
        // Pins still mismatched are inside their lockout: owe a wake at (accepted + delay + 1) ms,
        // the first tick at which `now - accepted > delay` holds. Recorded once per lockout.
        const uint32_t locked = (raw ^ c) & s_buttonMask;
        uint32_t fresh = locked & ~s_ledger.bitsValid;
        while (fresh) {
            const uint pin = (uint)__builtin_ctz(fresh);
            const uint32_t expiry = s_debounceTime[pin] + s_debounceDelay + 1u;
            s_ledger.bitUs[pin] = tus + (uint64_t)(expiry - now) * 1000u;
            fresh &= fresh - 1u;
        }
        s_ledger.bitsValid = locked;
        armNearest();
        restore_interrupts(save);
    } else {
        c = raw;                                 // straight through: the 1us floor path
        changed = (c != s_committed);
        s_committed = c;
    }
    return changed;
}

// Fires at an owed deadline: commit the window's collected inputs (or land a delayed release /
// an expired lockout) and stage the report, exactly on time, with the CPU otherwise asleep.
// Runs in TIMER_IRQ context; RAM-pinned.
void __not_in_flash_func(sync_alarm_cb)(uint /*alarm_num*/) {
    s_alarmArmed = false;   // this one has fired; stepSync() re-arms for whatever is still owed
    if (stepSync()) {
        auto fp = g_fastpath; if (fp) fp(s_committed);
        g_changed = true;   // the loop must re-read committed() so display/addons see the commit too
    }
}

// THE button-edge ISR, installed as the EXCLUSIVE IO_IRQ_BANK0 vector. It replaces the SDK's
// gpio_default_irq_handler, which (a) lives in flash, so the first edge after idle paid an XIP
// cache miss, and (b) calls the callback once PER PIN that fired, so a chord whose edges land in
// one IRQ stepped the window and rebuilt the report twice. Here: ack every pending button edge
// in one pass, step once, build once. We are the only IO_IRQ_BANK0 user in this firmware (the
// Maple bus is PIO IRQs, LEDs are PIO/DMA) -- nothing else may call gpio_set_irq_callback().
// RAM-pinned (hot ISR).
void __not_in_flash_func(doorbell_irq)() {
    LatencyProbe::edge();   // timestamp the raw button edge, first thing
    io_bank0_irq_ctrl_hw_t* ctrl = &io_bank0_hw->proc0_irq_ctrl;
    for (uint i = 0; i < (NUM_BANK0_GPIOS + 7u) / 8u; i++) {
        const uint32_t ints = ctrl->ints[i];      // only button pins are enabled, so all of it is ours
        if (ints) io_bank0_hw->intr[i] = ints;    // W1C: ack the edge bits in one store per register
    }
    // Step the front stage on the edge and, if the committed state changed, stage the report NOW
    // (the report is built on the EDGE's clock, not the loop's). A press inside an open sync
    // window doesn't change committed yet -- it commits from the alarm at the deadline.
    if (stepSync()) { auto fp = g_fastpath; if (fp) fp(s_committed); }
    g_edges++;
    g_changed = true;
}

} // namespace

namespace GpioDoorbell {

void init(uint32_t buttonMask) {
    s_buttonMask = buttonMask;
    if (!s_alarmInited) {
        // required=false: if no hardware alarm is free, s_alarm stays -1 and we degrade to the
        // loop-poll commit (committed() steps the window each iteration) -- slower but never a panic.
        s_alarm = hardware_alarm_claim_unused(false);
        if (s_alarm >= 0) hardware_alarm_set_callback((uint)s_alarm, &sync_alarm_cb);
        s_alarmInited = true;
    }
    if (!s_irqInstalled) {
        irq_set_exclusive_handler(IO_IRQ_BANK0, &doorbell_irq);
        s_irqInstalled = true;
    }
    for (uint pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        const bool on = (buttonMask & (1u << pin)) != 0u;
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, on);
    }
    // Tournament determinism: the button edge -> report path must be the FIRST thing the core
    // services, so an in-flight USB or timer IRQ can never delay it. Give the GPIO bank the highest
    // priority (0), same as the Maple bus -- this squeezes the worst-case edge->ISR jitter, not the
    // mean (mean is already the USB poll floor). Set before enabling so it's prioritized from boot.
    irq_set_priority(IO_IRQ_BANK0, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

bool __not_in_flash_func(consumeChanged)() {
    const uint32_t save = save_and_disable_interrupts();
    const bool c = g_changed;
    g_changed = false;
    restore_interrupts(save);
    return c;
}

uint32_t edgeCount() { return g_edges; }

void registerFastPath(void (*fn)(uint32_t committed)) { g_fastpath = fn; }

bool fastPathArmed() { return g_fastpath != nullptr; }

void configSync(bool active, const CoreInput::SyncPolicy& policy) {
    const uint32_t w = policy.window < 1u ? 1u
                     : (policy.window > SYNC_WINDOW_MAX ? SYNC_WINDOW_MAX : policy.window);
    // s_sync is stepped by the edge ISR and the alarm ISR, so publishing policy must be atomic
    // with respect to both -- a half-updated mask would be read by the very next button edge.
    const uint32_t save = save_and_disable_interrupts();
    if (!s_syncInited) { sync_window_init(&s_sync, w, policy.releaseDebounce); s_syncInited = true; }
    s_sync.window           = w;              // dynamic, like the incumbent re-read each call
    s_sync.release_debounce = policy.releaseDebounce;
    s_sync.synced_mask      = (buttons_t)policy.syncedMask;
    s_sync.attack_mask      = (buttons_t)policy.attackMask;
    s_sync.commit_at        = policy.eagerCommit;
    // See core_bridge.cpp: asserts are compiled out in Release, so resolve the two release
    // policies here instead of letting preserve_width silently win.
    s_sync.preserve_width   = policy.preserveWidth && !policy.releaseDebounce;
    if (s_syncActive != active) ledgerClear();   // mode switch: the ledger belongs to the other stage
    s_syncActive            = active;
    restore_interrupts(save);
}

void configDebounce(bool active, uint32_t delayMs) {
    const uint32_t save = save_and_disable_interrupts();
    s_debounceDelay  = delayMs;
    if (s_debounceActive != active) ledgerClear();
    s_debounceActive = active;
    restore_interrupts(save);
}

uint32_t committed() {
    // Step with the current pins too: catches a button held at boot (no edge to trigger the ISR)
    // and backstops the event-driven commit. Does NOT stage a report -- the loop's own path does.
    stepSync();
    return s_committed;
}

} // namespace GpioDoorbell
