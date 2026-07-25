/*
 * gpio_doorbell -- the input "doorbell". Enables rising+falling edge IRQs on ONLY the
 * button pins (the RP2040's native per-pin GPIO IRQ -- its EXTI), so the CPU can sleep and
 * wake on a real button edge instead of polling. Silicon watches the buttons.
 *
 * A4a of the offload (TE-OFFLOAD-PLAN.md). This is the WAKE SOURCE; A4b turns the poll loop
 * into WFE and consumes the flag. Until A4b, loop behavior is unchanged -- the doorbell only
 * flags "a button edge happened" and counts edges (a bench diagnostic).
 *
 * Masking matters: PIO can't cheaply mask scattered button pins, so I2C/LED/USB noise would
 * fire a PIO doorbell constantly. Per-pin GPIO IRQ enables edges on the button pins ONLY.
 */
#pragma once
#include <stdint.h>

namespace GpioDoorbell {
    // Arm edge IRQs on every pin set in buttonMask; disable them on all others. Idempotent
    // (safe to call again on a profile/mode re-init -- it re-derives from the new mask).
    void init(uint32_t buttonMask);

    // Atomically snapshot + clear the "a button edge happened since last check" flag.
    // For the event-driven loop (A4b): run the pipeline iff this (or a timer) fired.
    bool consumeChanged();

    // Total button edges observed (diagnostic; wraps). Lets a bench confirm the silicon
    // is seeing presses without touching the loop.
    uint32_t edgeCount();

    // Register a RAM-pinned callback run INSIDE the edge/timer ISRs -- the "fast path" (rung 2).
    // It is handed the sync-committed pressed mask and formats+stages its report immediately,
    // instead of waiting for the main loop to lap back to the read. fn must be short, RAM-pinned,
    // and touch only ISR-safe state. Pass nullptr to disarm. Single slot (one driver owns it).
    void registerFastPath(void (*fn)(uint32_t committed));

    // ---- Stage 4: the doorbell owns the NOBD sync window --------------------------------------
    // The sync window (co-registration front stage) runs here now, driven by the edge ISR + a
    // one-pulse hardware alarm, so a commit fires exactly at its deadline with the CPU asleep --
    // instead of the loop noticing it ~50us later. Configure it from the loop each iteration
    // (cheap, preserves state); active=false => the ISR passes pins straight through.
    void configSync(bool active, uint32_t window, bool releaseDebounce);

    // The current committed pressed mask (post sync/debounce). The loop reads this into
    // debouncedGpio so the whole system (USB report, display, addons) honors the SAME ISR-owned
    // front stage -- single owner, no divergence. Also steps the stage with the current pins
    // (catches a button held at boot before any edge, and backstops the event-driven commit).
    uint32_t committed();

    // Stage 3: the doorbell also owns per-pin leading-edge debounce (the ship-tier "fire on first
    // edge" -- accept a change immediately, lock the pin out for delayMs). Same algorithm as the
    // loop's debounceGpioGetAll(), just run on the EDGE, so a clean press lands in ~1us not ~70us.
    // Mutually exclusive with the sync window (the loop enables exactly one). active=false =>
    // pins pass straight through.
    void configDebounce(bool active, uint32_t delayMs);
}
