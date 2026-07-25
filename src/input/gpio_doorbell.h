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

    // Register a RAM-pinned callback run INSIDE the edge ISR -- the "fast path" (rung 2). On
    // every button edge the driver rebuilds + stages its report immediately, instead of waiting
    // for the main loop to lap back around to the read (the loop-cycle latency the bench proved
    // dominant: min 1us but avg 70us / max 1000us). fn must be short, RAM-pinned, and touch only
    // ISR-safe state. Pass nullptr to disarm. Single slot (one driver owns the fast path).
    void registerFastPath(void (*fn)());
}
