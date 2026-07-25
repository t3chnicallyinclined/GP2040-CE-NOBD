#include "gpio_doorbell.h"
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"   // save_and_disable_interrupts / restore_interrupts
#include "latency_probe.h"

namespace {

volatile uint32_t g_edges   = 0;
volatile bool     g_changed = false;

// The rung-2 fast path: a driver-supplied builder run right here in the edge ISR. Function
// pointer (single slot). Read/called from the ISR, written once from the loop at setup.
void (* volatile g_fastpath)() = nullptr;

// Runs in IO_IRQ_BANK0 context. The SDK's default GPIO handler acknowledges the pin IRQ
// before invoking this callback, so we only record the event. RAM-pinned (hot ISR).
void __not_in_flash_func(doorbell_cb)(uint /*gpio*/, uint32_t /*events*/) {
    LatencyProbe::edge();   // timestamp the raw button edge, first thing
    // FAST PATH (rung 2): rebuild + stage the report NOW, on the edge, so it's in the endpoint
    // in ~1us flat instead of waiting the avg 70us for the loop to lap back to the read. This
    // is the whole point -- the report is built on the EDGE's clock, not the loop's.
    if (g_fastpath) g_fastpath();
    g_edges++;
    g_changed = true;
}

} // namespace

namespace GpioDoorbell {

void init(uint32_t buttonMask) {
    gpio_set_irq_callback(&doorbell_cb);
    for (uint pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        const bool on = (buttonMask & (1u << pin)) != 0u;
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, on);
    }
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

void registerFastPath(void (*fn)()) { g_fastpath = fn; }

} // namespace GpioDoorbell
