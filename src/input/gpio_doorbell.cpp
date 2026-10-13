#include "gpio_doorbell.h"
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"    // save_and_disable_interrupts / restore_interrupts
#include "hardware/timer.h"   // hardware_alarm_* + time
#include "hardware/structs/io_bank0.h"
// NVIC ISPR, written inline (see armNearest). Same register on both cores, different header.
#if PICO_RP2040
#include "hardware/regs/m0plus.h"
#define DOORBELL_NVIC_ISPR_OFFSET M0PLUS_NVIC_ISPR_OFFSET
#else
#include "hardware/regs/m33.h"
#define DOORBELL_NVIC_ISPR_OFFSET M33_NVIC_ISPR0_OFFSET
#endif
// RP2350 has two timers; timer_hw / hardware_alarm_* are TIMER0, so its alarm IRQs are TIMER0_IRQ_n.
#if !PICO_RP2040
#define TIMER_IRQ_0 TIMER0_IRQ_0
#endif
#include "pico/time.h"        // to_ms_since_boot / get_absolute_time / from_us_since_boot
#include "latency_probe.h"

// The DST-proven sync-window core is C; give it C linkage (binds to sync_window.c).
extern "C" {
#include "core/sync_window.h"
}

namespace {

// ============================================================================================
// NO FLASH ON THE EDGE PATH.
//
// This ISR is RAM-pinned, but it was reaching flash SIX ways per button edge -- each through a
// long-branch veneer, each an XIP cache-miss risk on the first edge after idle, which is exactly
// the jitter a98e02ac ("RAM-pin the input hot path") set out to remove. MEASURED by disassembling
// the shipped ELF:
//
//     time_us_64                0x1007f694   <- get_absolute_time()
//     __wrap___aeabi_uldivmod   0x100804d4   <- to_ms_since_boot(): a 64-bit divide by 1000
//     __wrap___aeabi_lmul       (flash)      <- the ledger's 64-bit deadline multiply
//     __wrap___aeabi_uidiv      (flash)      <- a 32-bit divide
//     __wrap___ctzsi2           0x100803f0   <- ARMv6-M HAS NO CTZ INSTRUCTION
//     hardware_alarm_set_target 0x1007f858   <- arming the commit alarm
//
// The cost is also VARIABLE, which is worse than it being large: the SDK divider takes its fast
// path only while the dividend's high word is zero (uptime under ~71.6 minutes) and the SIO
// divider is not dirty; otherwise it save/restores and takes the 64-bit path. Variable cost in
// flash on the hot path is a jitter profile.
//
// The replacements below are all inline, allocate no shared state, and keep the tick in
// MILLISECONDS exactly as before -- the core, the loop's own sync instance and the ledger are
// untouched. (A microsecond tick would be cheaper still, but the loop keeps a SEPARATE
// sync_window_t on getMillis(), and making only the ISR us-precise lets the loop's process()
// overwrite the ISR's DPRAM value up to a millisecond late. Converting both is a bigger change
// than this one and belongs on its own branch.)
// ============================================================================================

// One 32-bit load of the free-running microsecond counter. TIMERAWL reads the low word with no
// latching and no side effects, so unlike time_us_64() it needs no helper and cannot be
// interrupted mid-read (CONFIRMED: RP2040 datasheet, TIMERAWL).
inline uint32_t raw_us() { return timer_hw->timerawl; }

// High 32 bits of a 32x32 product. ARMv6-M has MULS (32x32 -> low 32, one cycle) but no UMULL,
// so the halves are combined by hand. Pure arithmetic: no call, no flash, no shared state.
inline uint32_t mulhi32(uint32_t a, uint32_t b) {
    const uint32_t al = a & 0xFFFFu, ah = a >> 16;
    const uint32_t bl = b & 0xFFFFu, bh = b >> 16;
    uint32_t t  = al * bl;
    uint32_t k  = t >> 16;
    t = ah * bl + k;  k = t & 0xFFFFu;  const uint32_t w1 = t >> 16;
    t = al * bh + k;  k = t >> 16;
    return ah * bh + w1 + k;
}

// floor(v / 1000) for the whole uint32 range, by reciprocal multiply: 2^38/1000 = 0x10624DD3,
// so v/1000 == (v * 0x10624DD3) >> 38 == mulhi32(v, 0x10624DD3) >> 6. Replaces the 64-bit
// __aeabi_uldivmod call, and unlike the SIO hardware divider it holds no state, so it is safe in
// an ISR that can interrupt a division in the loop without any save/restore.
inline uint32_t div1000(uint32_t v) { return mulhi32(v, 0x10624DD3u) >> 6; }

// Count trailing zeros, inline. __builtin_ctz on ARMv6-M compiles to a CALL into a flash-resident
// libgcc helper -- on this path, per changed bit. De Bruijn multiply + a 32-entry table is a
// handful of inline instructions. v MUST be non-zero (every caller masks first).
const uint8_t kCtz32[32] = {
     0,  1, 28,  2, 29, 14, 24,  3, 30, 22, 20, 15, 25, 17,  4,  8,
    31, 27, 13, 23, 21, 19, 16,  7, 26, 12, 18,  6, 11,  5, 10,  9
};
inline uint32_t ctz32(uint32_t v) {
    return kCtz32[(uint32_t)(((v & (uint32_t)(-(int32_t)v)) * 0x077CB531u) >> 27)];
}

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
// Targets are 32-bit microseconds, not 64. The ALARM COMPARE IS 32 BITS WIDE in silicon
// (datasheet: "the alarm fires when TIMER_ALARM0 == TIMELR"), and the SDK truncates to 32 bits
// anyway -- so the 64-bit arithmetic only ever existed to satisfy a 64-bit API. Dropping it turns
// the ledger's 64-bit deadline multiply into a one-cycle MULS and removes the lmul helper. Every
// comparison below is a SIGNED DIFFERENCE, so the ~71.6 minute wrap of a u32 microsecond counter
// is handled rather than merely survived.
struct Ledger {
    bool     pressValid;   uint32_t pressMs;   uint32_t pressUs;    // s_sync.open / deadline
    bool     relValid;     uint32_t relMs;     uint32_t relUs;      // s_sync.release_open / release_deadline
    uint32_t bitsValid;                                             // per-bit records live for these bits
    uint32_t bitUs[NUM_BANK0_GPIOS];                                // rel_due (sync) or lockout expiry (debounce)
};
Ledger        s_ledger = {};
volatile bool s_alarmArmed = false;        // the hardware alarm is set for s_alarmUs
uint32_t      s_alarmUs    = 0;

inline void ledgerClear() {
    s_ledger.pressValid = false;
    s_ledger.relValid   = false;
    s_ledger.bitsValid  = 0;
}

// Point the one-pulse alarm at the nearest recorded target. Inside the critical section.
//
// Arming is now ONE STORE to the alarm register. Writing timer_hw->alarm[n] both sets the compare
// value and arms it, and the compare is 32-bit against TIMELR -- which is exactly the shape of a
// 32-bit microsecond target. hardware_alarm_set_target() did the same store underneath, but lives
// in flash and takes a 64-bit timestamp, which is what dragged time_us_64 + a 64-bit multiply
// onto this path.
inline void __not_in_flash_func(armNearest)() {
    if (s_alarm < 0) return;
    bool any = false;
    uint32_t best = 0;
    if (s_ledger.pressValid) { best = s_ledger.pressUs; any = true; }
    if (s_ledger.relValid && (!any || (int32_t)(s_ledger.relUs - best) < 0)) { best = s_ledger.relUs; any = true; }
    uint32_t bits = s_ledger.bitsValid;
    while (bits) {
        const uint b = (uint)ctz32(bits);
        if (!any || (int32_t)(s_ledger.bitUs[b] - best) < 0) { best = s_ledger.bitUs[b]; any = true; }
        bits &= bits - 1u;
    }
    if (!any) {                                     // nothing owed
        s_alarmArmed = false;
        timer_hw->armed = 1u << (uint)s_alarm;      // W1C: disarm, so no stale alarm fires
        return;
    }
    if (s_alarmArmed && best == s_alarmUs) return;  // already armed for exactly this instant
    s_alarmUs = best;
    timer_hw->alarm[(uint)s_alarm] = best;          // one store: sets the compare AND arms
    s_alarmArmed = true;
    // A target already reached will NOT match again for ~71 minutes -- that is the stuck-input
    // failure mode this whole ledger exists to prevent. If we were held off past it, disarm and
    // pend the vector so the handler re-steps immediately instead of trusting an alarm that is
    // never coming. irq_set_pending is an inline NVIC store, not a call.
    if ((int32_t)(raw_us() - best) >= 0) {
        timer_hw->armed = 1u << (uint)s_alarm;
        s_alarmArmed = false;
        // NVIC set-pending, written inline: the SDK's irq_set_pending() is flash-resident, and
        // this is precisely the recovery path that must not stall on an XIP miss.
        *(volatile uint32_t*)(PPB_BASE + DOORBELL_NVIC_ISPR_OFFSET) =
            1u << ((uint)(TIMER_IRQ_0 + s_alarm) & 0x1Fu);
    }
}

// Step the input front stage with the current pins, update s_committed, and arm the one-pulse
// alarm at the nearest deadline the stage still owes. Returns true if committed changed.
// RAM-pinned. When sync is OFF this is the validated ~1us straight-through path (no now/CS/alarm).
bool __not_in_flash_func(stepSync)() {
    const uint32_t raw = (~gpio_get_all()) & s_buttonMask;   // active-low -> pressed=1, masked
    uint32_t c;
    bool changed;

    if (s_syncActive) {
        const uint32_t tus       = raw_us();          // ONE 32-bit load: us for the ledger...
        const uint32_t now       = div1000(tus);      // ...ms for the core, by inline reciprocal
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
                s_ledger.pressUs    = tus + (s_sync.deadline - now) * 1000u;   // exact: this IS the edge
            }
        } else {
            s_ledger.pressValid = false;
        }
        if (s_sync.release_open) {
            if (!s_ledger.relValid || s_ledger.relMs != s_sync.release_deadline) {
                s_ledger.relValid = true;
                s_ledger.relMs    = s_sync.release_deadline;
                s_ledger.relUs    = tus + (s_sync.release_deadline - now) * 1000u;
            }
        } else {
            s_ledger.relValid = false;
        }
        {   // preserve-width: a bit entering `releasing` owes exactly its own delay from THIS edge
            uint32_t fresh = (uint32_t)s_sync.releasing & ~s_ledger.bitsValid;
            while (fresh) {
                const uint b = (uint)ctz32(fresh);
                s_ledger.bitUs[b] = tus + (s_sync.rel_due[b] - now) * 1000u;
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
        const uint32_t tus       = raw_us();
        const uint32_t now       = div1000(tus);
        const uint32_t save = save_and_disable_interrupts();
        const uint32_t before = s_committed;
        uint32_t chg = (raw ^ before) & s_buttonMask;
        while (chg) {
            const uint pin = (uint)ctz32(chg);
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
            const uint pin = (uint)ctz32(fresh);
            const uint32_t expiry = s_debounceTime[pin] + s_debounceDelay + 1u;
            s_ledger.bitUs[pin] = tus + (expiry - now) * 1000u;
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
// Installed as the EXCLUSIVE TIMER_IRQ_<alarm> vector rather than going through the SDK's
// hardware_alarm callback shim, so the vector lands straight here with no flash-resident
// dispatch -- the same reasoning as the exclusive IO_IRQ_BANK0 handler below. RAM-pinned.
void __not_in_flash_func(sync_alarm_irq)() {
    timer_hw->intr = 1u << (uint)s_alarm;   // W1C: ack this alarm
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
        if (s_alarm >= 0) {
            // Claim reserves the alarm number; we then take its vector outright instead of
            // registering an SDK callback, and enable the alarm's interrupt at the timer.
            irq_set_exclusive_handler((uint)(TIMER_IRQ_0 + s_alarm), &sync_alarm_irq);
            irq_set_enabled((uint)(TIMER_IRQ_0 + s_alarm), true);
            hw_set_bits(&timer_hw->inte, 1u << (uint)s_alarm);
        }
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
