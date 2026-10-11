/*
 * SPDX-License-Identifier: MIT
 *
 * NOBD HID -- a STANDARD HID gamepad whose report is a DIRECT BIT IMAGE of the button bank.
 *
 * WHY THIS EXISTS. Every other personality here spends ~150 instructions per edge turning a pin
 * mask into console-specific wire bits: XInputDriver's xinput_straight() is 17 sequential
 * `if (p & pinmask) b |= reportbit` tests plus invert, 4-way and SOCD stages. That cost is not the
 * problem -- a 1 ms Full-Speed poll dwarfs it -- but its VARIABILITY is, and determinism is the
 * product. On this board the buttons are GPIO 5..22, FULLY CONTIGUOUS, so if the report's bit
 * order is chosen to follow pin order the whole formatter collapses to three shifts, one table
 * load and three stores. Bounded, branch-free, identical every time.
 *
 * THE DESCRIPTOR IS NOT CUSTOM, DELIBERATELY. It is the generic HID gamepad report descriptor,
 * reused verbatim (32 buttons + hat + four 8-bit axes), because that layout ALREADY gives each
 * field its own aligned slot:
 *
 *     offset 0..1   halfword   buttons 1..16    <- pins 5..15  (11 bits), one shift
 *     offset 2..3   halfword   buttons 17..32   <- pins 20..22 (3 bits),  one shift
 *     offset 4      byte       hat:4 + pad:4    <- pins 16..19 via a 16-entry table
 *     offset 5..8   bytes      four axes        <- constant for a digital stick
 *
 * Only the DEVICE descriptor (ids, strings) is ours. Keeping the report descriptor standard means
 * no host-compatibility risk: it enumerates as an ordinary DirectInput gamepad everywhere.
 *
 * Each field above is an INDEPENDENT, ALIGNED write to bytes nothing else touches. That is the
 * property the zero-CPU path needs (see docs/roadmog/zero-cpu-report-path.md): a PIO state machine
 * per contiguous pin run, each feeding a DMA that patches its own slot, with no CPU in the path at
 * all. XInput can never do this -- it packs the d-pad and Start/Back/LS/RS into the same byte 2,
 * and DMA cannot OR. This driver is that architecture's prerequisite, built CPU-side first so the
 * mapping can be proven before any DMA exists.
 *
 * HONEST COST. The button ORDER follows pin order, not convention, so a game's default bindings
 * will be wrong and the player must remap once. For a fight stick that is normal. In exchange the
 * report build is bounded and the layout is DMA-patchable.
 */

#ifndef _NOBD_HID_DRIVER_H_
#define _NOBD_HID_DRIVER_H_

#include "gpdriver.h"
#include "drivers/hid/HIDDescriptors.h"

// ---- the NOBD mapping: report bit position <- GPIO pin -------------------------------------
// Three contiguous pin runs on RP2040AdvancedBreakoutBoard. Each becomes one shift.
#define NOBD_RUN_A_PIN_BASE   5u    // L2 R2 B2 B1 L1 R1 B4 B3 S2 A1 S1  -> buttons 1..11
#define NOBD_RUN_A_WIDTH     11u
#define NOBD_RUN_B_PIN_BASE  20u    // A2 L3 R3                          -> buttons 17..19
#define NOBD_RUN_B_WIDTH      3u
#define NOBD_DIR_PIN_BASE    16u    // LEFT RIGHT DOWN UP                -> the hat
#define NOBD_DIR_WIDTH        4u

// Byte offsets of the three independently-writable slots inside HIDReport.
#define NOBD_OFF_BTN_LO       0u    // halfword: buttons 1..16
#define NOBD_OFF_BTN_HI       2u    // halfword: buttons 17..32
#define NOBD_OFF_HAT          4u    // byte:     hat nibble + padding

class NOBDHIDDriver : public GPDriver {
public:
    virtual void initialize();
    virtual bool process(Gamepad * gamepad);
    virtual void initializeAux() {}
    virtual void processAux() {}
    virtual uint16_t get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
    virtual void set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
    virtual bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
    virtual const uint16_t * get_descriptor_string_cb(uint8_t index, uint16_t langid);
    virtual const uint8_t * get_descriptor_device_cb();
    virtual const uint8_t * get_hid_descriptor_report_cb(uint8_t itf);
    virtual const uint8_t * get_descriptor_configuration_cb(uint8_t index);
    virtual const uint8_t * get_descriptor_device_qualifier_cb();
    virtual uint16_t GetJoystickMidValue();
    virtual USBListener * get_usb_auth_listener() { return nullptr; }
private:
    uint8_t   last_report[CFG_TUD_ENDPOINT0_SIZE] = { };
    HIDReport hidReport;
};

#endif // _NOBD_HID_DRIVER_H_
