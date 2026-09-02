#include "gpio_doorbell.h"
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"    // save_and_disable_interrupts / restore_interrupts
#include "hardware/timer.h"   // hardware_alarm_* + time
#include "pico/time.h"        // to_ms_since_boot / get_absolute_time / make_timeout_time_ms
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
volatile bool     s_debounceActive = false;                    // Stage 3: leading-edge debounce
uint32_t          s_debounceDelay  = 0;                        // lockout ms
uint32_t          s_debounceTime[NUM_BANK0_GPIOS] = {0};       // per-pin last accepted-edge ms

inline uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// Step the input front stage with the current pins, update s_committed, and (if a window is
// open) arm the one-pulse alarm at the nearest deadline. Returns true if committed changed.
// RAM-pinned. When sync is OFF this is the validated ~1us straight-through path (no now/CS/alarm).
bool __not_in_flash_func(stepSync)() {
    const uint32_t raw = (~gpio_get_all()) & s_buttonMask;   // active-low -> pressed=1, masked
    uint32_t c;
    bool changed;
    uint32_t next = 0;

    if (s_syncActive) {
        const uint32_t now = now_ms();
        // s_sync is touched by the edge ISR, the alarm ISR, AND the loop -- a short critical
        // section makes the step + its commit atomic across all three (single owner, no tear).
        const uint32_t save = save_and_disable_interrupts();
        c = (uint32_t)sync_window_step(&s_sync, now, (buttons_t)raw);
        const uint32_t pc = s_sync.open         ? (s_sync.deadline - now)         : 0u;
        const uint32_t rc = s_sync.release_open ? (s_sync.release_deadline - now) : 0u;
        next = (pc && rc) ? (pc < rc ? pc : rc) : (pc ? pc : rc);   // nearest pending deadline
        changed = (c != s_committed);
        s_committed = c;
        restore_interrupts(save);
    } else if (s_debounceActive) {
        // Leading-edge per-pin debounce: accept a pin's change immediately, then lock that pin out
        // for delayMs (identical algorithm to GP2040's debounceGpioGetAll, but run on the edge).
        // Only walks pins that actually changed. No alarm needed -- a change that settles DURING a
        // lockout (no fresh edge) is picked up by the committed()/loop poll, exactly as before.
        const uint32_t now = now_ms();
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
        restore_interrupts(save);
    } else {
        c = raw;                                 // straight through: the 1us floor path
        changed = (c != s_committed);
        s_committed = c;
    }

    if (next && s_alarm >= 0) {
        hardware_alarm_set_target((uint)s_alarm, make_timeout_time_ms(next));
    }
    return changed;
}

// Fires at a co-registration deadline: commit the window's collected inputs and stage the report,
// exactly on time, with the CPU otherwise asleep. Runs in TIMER_IRQ context; RAM-pinned.
void __not_in_flash_func(sync_alarm_cb)(uint /*alarm_num*/) {
    if (stepSync()) { auto fp = g_fastpath; if (fp) fp(s_committed); }
}

// Runs in IO_IRQ_BANK0 context. The SDK's default GPIO handler ACKs the pin IRQ before invoking
// this, so we only react. RAM-pinned (hot ISR).
void __not_in_flash_func(doorbell_cb)(uint /*gpio*/, uint32_t /*events*/) {
    LatencyProbe::edge();   // timestamp the raw button edge, first thing
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
    gpio_set_irq_callback(&doorbell_cb);
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
    s_syncActive            = active;
    restore_interrupts(save);
}

void configDebounce(bool active, uint32_t delayMs) {
    const uint32_t save = save_and_disable_interrupts();
    s_debounceDelay  = delayMs;
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
