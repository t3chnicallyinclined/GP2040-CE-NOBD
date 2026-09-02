/*
 * Track-B bulk-probe firmware (RP2040, standalone).
 *
 * Streams a 16-byte payload -- [0..3] sequence counter, [4..7] device time (us), [8..15] pad --
 * onto a WinUSB bulk IN endpoint as fast as the host drains it. A PC probe (pc-probe/) tight-polls
 * that endpoint and measures the ACHIEVED rate + drop-free sequence: the make-or-break Phase 0
 * question for Track B (can we pull data off USB-FS faster than the 1 kHz interrupt-poll floor?).
 *
 * Isolated from the gp2040-te XInput driver on purpose -- same RP2040 + USB-FS, so the rate it
 * proves is the rate the stick could hit. If Phase 0 says ~8-16 kHz, we integrate it for real.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define PAYLOAD 16

int main(void) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    tud_init(BOARD_TUD_RHPORT);

    uint32_t seq = 0;
    uint8_t  buf[PAYLOAD];
    uint32_t led_t = 0;

    while (true) {
        tud_task();

        // Keep the bulk IN fed: whenever there's TX FIFO room, stamp + queue the next payload.
        // The host's read rate then equals the achievable poll rate (nothing waiting on the device).
        if (tud_vendor_mounted() && tud_vendor_write_available() >= PAYLOAD) {
            const uint32_t ts = time_us_32();
            memcpy(buf + 0, &seq, 4);
            memcpy(buf + 4, &ts,  4);
            memset(buf + 8, 0, 8);
            tud_vendor_write(buf, PAYLOAD);
            tud_vendor_write_flush();
            seq++;
        }

#ifdef PICO_DEFAULT_LED_PIN
        const uint32_t now = time_us_32();
        if (now - led_t > 250000u) { led_t = now; gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN); }
#endif
    }
}

// Serve the Microsoft OS 2.0 descriptor so Windows auto-binds WinUSB (no driver install).
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == VENDOR_REQUEST_MICROSOFT && request->wIndex == 7) {
        uint16_t total_len;
        memcpy(&total_len, desc_ms_os_20 + 8, 2);   // total length field of the set header
        return tud_control_xfer(rhport, request, (void*)(uintptr_t)desc_ms_os_20, total_len);
    }
    return false;
}
