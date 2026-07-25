#include "gpio_doorbell.h"
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"   // save_and_disable_interrupts / restore_interrupts

namespace {

volatile uint32_t g_edges   = 0;
volatile bool     g_changed = false;

// Runs in IO_IRQ_BANK0 context. The SDK's default GPIO handler acknowledges the pin IRQ
// before invoking this callback, so we only record the event. RAM-pinned (hot ISR).
void __not_in_flash_func(doorbell_cb)(uint /*gpio*/, uint32_t /*events*/) {
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

} // namespace GpioDoorbell
