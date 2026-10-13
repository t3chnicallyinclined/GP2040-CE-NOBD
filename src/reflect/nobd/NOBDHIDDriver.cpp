/*
 * SPDX-License-Identifier: MIT
 *
 * NOBD HID. See headers/drivers/nobd/NOBDHIDDriver.h for why the report is a direct bit image
 * of the button bank and why the DESCRIPTOR is deliberately the standard one.
 */

#include "drivers/nobd/NOBDHIDDriver.h"
#include "drivers/hid/HIDDescriptors.h"
#include "drivers/shared/driverhelper.h"
#include "storagemanager.h"
#include "input/gpio_doorbell.h"

#include "pico.h"
// The DPRAM address of an IN endpoint's buffer. Resolved loop-side and cached, never from the ISR.
extern "C" uint8_t* dcd_rp2040_ep_dpram(uint8_t ep_addr);

// ---- NOBD device identity (the REPORT descriptor stays the standard one) -------------------
#define NOBD_VENDOR_ID   0x10C4
#define NOBD_PRODUCT_ID  0x82C1   // generic-HID sibling id; 0x82C0 is the stock Generic mode

static const uint8_t nobd_string_language[]     = { 0x09, 0x04 };
static const uint8_t nobd_string_manufacturer[] = "NOBD";
static const uint8_t nobd_string_product[]      = "NOBD-ZERO";
static const uint8_t nobd_string_version[]      = "1.0";

static const uint8_t *nobd_string_descriptors[] =
{
    nobd_string_language,
    nobd_string_manufacturer,
    nobd_string_product,
    nobd_string_version
};

static const uint8_t nobd_device_descriptor[] =
{
    18, 1,
    0x00, 0x02,
    0, 0, 0,
    HID_ENDPOINT_SIZE,
    LSB(NOBD_VENDOR_ID),  MSB(NOBD_VENDOR_ID),
    LSB(NOBD_PRODUCT_ID), MSB(NOBD_PRODUCT_ID),
    0x00, 0x01,
    1, 2, 0, 1
};

namespace {

// ---- THE DIRECTION TABLE ------------------------------------------------------------------
// 16 entries: the four direction pins, SOCD-resolved AND hat-encoded, pre-computed.
//
// This is the whole point. SOCD stops being COMPUTED and becomes a LOOKUP, so the hot path has
// no branches for it and a mode change is a POINTER SWAP, not a different code path. It also
// makes the direction slot reachable by a DMA later: PIO cannot do AND/OR/XOR, so SOCD-as-logic
// could never be offloaded, but SOCD-as-a-table can.
//
// Index bits follow the pins: bit0 = LEFT (16), bit1 = RIGHT (17), bit2 = DOWN (18), bit3 = UP (19).
// Values are HID hat codes (0=UP, 1=UPRIGHT, 2=RIGHT, ... 7=UPLEFT, 8=NOTHING).
const uint8_t kHatNeutral[16] = {
    /* 0000 ---- */ HID_HAT_NOTHING,
    /* 0001 L    */ HID_HAT_LEFT,
    /* 0010 R    */ HID_HAT_RIGHT,
    /* 0011 LR   */ HID_HAT_NOTHING,     // SOCD: left+right cancel
    /* 0100 D    */ HID_HAT_DOWN,
    /* 0101 DL   */ HID_HAT_DOWNLEFT,
    /* 0110 DR   */ HID_HAT_DOWNRIGHT,
    /* 0111 DLR  */ HID_HAT_DOWN,        // L+R cancel, D survives
    /* 1000 U    */ HID_HAT_UP,
    /* 1001 UL   */ HID_HAT_UPLEFT,
    /* 1010 UR   */ HID_HAT_UPRIGHT,
    /* 1011 ULR  */ HID_HAT_UP,          // L+R cancel, U survives
    /* 1100 UD   */ HID_HAT_NOTHING,     // SOCD neutral: up+down cancel
    /* 1101 UDL  */ HID_HAT_LEFT,        // U+D cancel, L survives
    /* 1110 UDR  */ HID_HAT_RIGHT,
    /* 1111 UDLR */ HID_HAT_NOTHING,
};
// Up-priority differs ONLY where up+down are both held -- the same table with four entries
// changed, which is exactly why a mode switch is a pointer swap.
const uint8_t kHatUpPriority[16] = {
    HID_HAT_NOTHING, HID_HAT_LEFT, HID_HAT_RIGHT, HID_HAT_NOTHING,
    HID_HAT_DOWN, HID_HAT_DOWNLEFT, HID_HAT_DOWNRIGHT, HID_HAT_DOWN,
    HID_HAT_UP, HID_HAT_UPLEFT, HID_HAT_UPRIGHT, HID_HAT_UP,
    HID_HAT_UP,                       // 1100 UD    -> UP wins
    HID_HAT_UPLEFT,                   // 1101 UDL   -> UP + L
    HID_HAT_UPRIGHT,                  // 1110 UDR   -> UP + R
    HID_HAT_UP,                       // 1111 UDLR  -> UP wins
};

// Published by the loop before the ISR can next run; a pointer store is atomic on M0+.
const uint8_t* volatile s_hat = kHatNeutral;

volatile bool  s_fastReady  = false;
uint8_t        s_fastEpIn   = 0;
uint8_t* volatile s_fastDpram = nullptr;   // refreshed loop-side every pass (see XInput's note)

// ---- THE FAST PATH -------------------------------------------------------------------------
// Three shifts, one table load, three stores. No branches, no per-bit tests, no stages.
//
// `committed` is PIN-INDEXED (bit n = pin n pressed), already active-low-corrected, masked and
// run through the sync window by the doorbell -- so the entire formatter is the bit rearrangement
// below. Compare XInputDriver::xinput_straight(): 17 conditional tests plus four stages.
//
// Every store is ALIGNED and owns its bytes, so the USB SIE -- which reads this buffer during the
// IN transaction -- can never observe a half-written field. That is the same reasoning as the
// two-halfword XInput stamp, and it is what makes each slot independently DMA-patchable.
void __not_in_flash_func(nobd_fastpath)(uint32_t committed) {
    if (!s_fastReady) return;
    uint8_t* dpram = s_fastDpram;
    if (dpram == nullptr) return;

    const uint32_t lo  = (committed >> NOBD_RUN_A_PIN_BASE) & ((1u << NOBD_RUN_A_WIDTH) - 1u);
    const uint32_t hi  = (committed >> NOBD_RUN_B_PIN_BASE) & ((1u << NOBD_RUN_B_WIDTH) - 1u);
    const uint8_t  hat = s_hat[(committed >> NOBD_DIR_PIN_BASE) & 0x0Fu];

    // All 32 button bits in ONE word store (the buffer is 64-byte aligned, so offset 0 is
    // word-aligned): both halves land together instead of in two halfword stores.
    *(volatile uint32_t*)(dpram + NOBD_OFF_BTN_LO) = lo | (hi << 16);
    *(volatile uint8_t *)(dpram + NOBD_OFF_HAT)    = hat;
}

// The same arithmetic the ISR runs, for the loop path -- ONE expression, used by both, so the two
// cannot drift. Fed from gamepad->debouncedGpio, which is the same pin-indexed mask the doorbell
// publishes, so loop output and ISR output are identical by construction rather than by a proof.
inline void nobd_build(uint32_t pins, HIDReport& r) {
    r.buttons  = ((pins >> NOBD_RUN_A_PIN_BASE) & ((1u << NOBD_RUN_A_WIDTH) - 1u))
               | (((pins >> NOBD_RUN_B_PIN_BASE) & ((1u << NOBD_RUN_B_WIDTH) - 1u)) << 16);
    r.direction = s_hat[(pins >> NOBD_DIR_PIN_BASE) & 0x0Fu];
}

bool nobd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
    return hidd_control_xfer_cb(rhport, stage, request);
}

// ALWAYS-ARMED, from the USB IRQ (TinyUSB 0.21's xfer_isr hook) -- the same discipline as
// XInputDriver. The stock HID class only re-armed when the loop called tud_hid_report(), so after
// each report the endpoint sat idle until the next loop pass: an ISR stamp on an idle endpoint
// did not ship until then. Now the endpoint is re-armed with the report still in DPRAM the moment
// it goes out, so the fast path's stamp is what the host reads on its very next poll. OUT/other
// endpoints, or anything that can't re-arm here, defer to the stock hidd_xfer_cb.
bool __not_in_flash_func(nobd_xfer_isr)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;
    (void)xferred_bytes;
    if (ep_addr != (0x80 | GAMEPAD_ENDPOINT) || !tud_ready())
        return false;
    uint8_t* dpram = dcd_rp2040_ep_dpram(ep_addr);
    if (dpram == nullptr || !usbd_edpt_claim(rhport, ep_addr))
        return false;
    const uint32_t seq = GpioDoorbell::stampSeq();
    usbd_edpt_xfer(rhport, ep_addr, dpram, (uint16_t)sizeof(HIDReport), true);
    usbd_edpt_release(rhport, ep_addr);
    GpioDoorbell::restamp(seq);   // the re-arm self-copies byte-wise; re-apply a stamp it reverted
    return true;
}

} // namespace

void NOBDHIDDriver::initialize() {
    hidReport = {
        .buttons = 0,
        .direction = HID_HAT_NOTHING,
        .dummy = 0,
        .l_x_axis = HID_JOYSTICK_MID, .l_y_axis = HID_JOYSTICK_MID,
        .r_x_axis = HID_JOYSTICK_MID, .r_y_axis = HID_JOYSTICK_MID,
    };

    class_driver = {
    #if CFG_TUSB_DEBUG >= 2
        .name = "NOBDHID",
    #endif
        .init = hidd_init,
        .reset = hidd_reset,
        .open = hidd_open,
        .control_xfer_cb = nobd_control_xfer_cb,
        .xfer_cb = hidd_xfer_cb,
        .xfer_isr = nobd_xfer_isr,
        .sof = NULL
    };
}

bool NOBDHIDDriver::process(Gamepad * gamepad) {
    // Publish the SOCD table before the ISR can next run. A pointer store is atomic on M0+, and
    // either table is self-consistent, so there is no torn state to guard.
    const SOCDMode socd = Gamepad::resolveSOCDMode(Storage::getInstance().getGamepadOptions());
    s_hat = (socd == SOCD_MODE_UP_PRIORITY) ? kHatUpPriority : kHatNeutral;

    nobd_build(gamepad->debouncedGpio, hidReport);
    // Axes stay centred: this personality is digital. A real analog stick is out of scope here
    // precisely because it would not be a bit image, and the whole point is that it is.
    hidReport.l_x_axis = HID_JOYSTICK_MID; hidReport.l_y_axis = HID_JOYSTICK_MID;
    hidReport.r_x_axis = HID_JOYSTICK_MID; hidReport.r_y_axis = HID_JOYSTICK_MID;

    if (tud_suspended())
        tud_remote_wakeup();

    // Arm the ISR fast path once the endpoint exists, and refresh the cached DPRAM pointer every
    // pass so a bus reset that re-allocates DPRAM can never leave the ISR writing someone else's
    // buffer (same discipline as XInputDriver).
    if (tud_ready()) {
        if (!s_fastReady) {
            s_fastEpIn = 0x80 | GAMEPAD_ENDPOINT;
            uint8_t* d = dcd_rp2040_ep_dpram(s_fastEpIn);
            if (d != nullptr) {
                s_fastDpram = d;
                s_fastReady = true;                    // set LAST: the ISR checks this first
                GpioDoorbell::registerFastPath(&nobd_fastpath);
            }
        } else {
            s_fastDpram = dcd_rp2040_ep_dpram(s_fastEpIn);
        }
    }

    void * report = &hidReport;
    uint16_t report_size = sizeof(hidReport);
    if (memcmp(last_report, report, report_size) != 0) {
        if (tud_hid_ready()) {
            // Endpoint idle (first report, or nothing re-armed it): queue normally.
            if (tud_hid_report(0, report, report_size) == true) {
                GpioDoorbell::restamp(GpioDoorbell::seqAtLastRead());
                memcpy(last_report, report, report_size);
                return true;
            }
        } else if (tud_ready() && s_fastDpram != nullptr) {
            // Endpoint armed (the normal case now): freshen the staged report in place, then
            // re-apply any ISR stamp newer than the pins this report was built from.
            memcpy(s_fastDpram, report, report_size);
            GpioDoorbell::restamp(GpioDoorbell::seqAtLastRead());
            memcpy(last_report, report, report_size);
            return true;
        }
    }
    return false;
}

uint16_t NOBDHIDDriver::get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    memcpy(buffer, &hidReport, sizeof(HIDReport));
    return sizeof(HIDReport);
}

void NOBDHIDDriver::set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {}

bool NOBDHIDDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    return false;
}

const uint16_t * NOBDHIDDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
    char *value;
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    if (gamepadOptions.usbDescOverride == true) {
        switch (index) {
            case 1:  value = gamepadOptions.usbDescManufacturer; break;
            case 2:  value = gamepadOptions.usbDescProduct;      break;
            case 3:  value = gamepadOptions.usbDescVersion;      break;
            default: value = (char *)nobd_string_descriptors[index]; break;
        }
    } else {
        value = (char *)nobd_string_descriptors[index];
    }
    return getStringDescriptor(value, index);
}

const uint8_t * NOBDHIDDriver::get_descriptor_device_cb() {
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    if (gamepadOptions.usbOverrideID == true) {
        static uint8_t modified[18];
        memcpy(modified, nobd_device_descriptor, sizeof(nobd_device_descriptor));
        memcpy(&modified[8],  (uint8_t*)&gamepadOptions.usbVendorID,  sizeof(uint16_t));
        memcpy(&modified[10], (uint8_t*)&gamepadOptions.usbProductID, sizeof(uint16_t));
        return (const uint8_t*)modified;
    }
    return nobd_device_descriptor;
}

// The REPORT descriptor is the standard generic-HID one, on purpose: the NOBD part is the bit
// ASSIGNMENT, not the descriptor, so there is no host-compatibility risk to carry.
const uint8_t * NOBDHIDDriver::get_hid_descriptor_report_cb(uint8_t itf) {
    return hid_report_descriptor;
}

const uint8_t * NOBDHIDDriver::get_descriptor_configuration_cb(uint8_t index) {
    return hid_configuration_descriptor;
}

const uint8_t * NOBDHIDDriver::get_descriptor_device_qualifier_cb() {
    return nullptr;
}

uint16_t NOBDHIDDriver::GetJoystickMidValue() {
    return HID_JOYSTICK_MID << 8;
}
