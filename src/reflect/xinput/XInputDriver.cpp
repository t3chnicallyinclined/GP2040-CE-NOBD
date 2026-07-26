/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "drivers/xinput/XInputDriver.h"
#include "drivers/shared/driverhelper.h"
#include "storagemanager.h"
#include "input/latency_probe.h"
#include "input/gpio_doorbell.h"
#include "pico.h"              // __not_in_flash_func (RAM-pin the ISR fast path)
#include "hardware/gpio.h"    // gpio_get_all (read the pins straight, in the edge ISR)
#include <cstddef>            // offsetof (split report content from the measurement-stamp region)

// NOBD hot-buffer hook exposed from the vendored TinyUSB rp2040 driver (dcd_rp2040.c):
// the DPRAM buffer the SIE sends from, so we can freshen the staged report in place.
extern "C" uint8_t* dcd_rp2040_ep_dpram(uint8_t ep_addr);

// Set by the main loop (gp2040.cpp) each frame: did any addon modify the report? The ISR fast
// path bypasses addons, so its gate disarms when this is true. See GP2040::run().
extern volatile bool g_teAddonTouchedInputs;

#define USB_SETUP_DEVICE_TO_HOST 0x80
#define USB_SETUP_HOST_TO_DEVICE 0x00
#define USB_SETUP_TYPE_VENDOR    0x40
#define USB_SETUP_TYPE_CLASS     0x20
#define USB_SETUP_TYPE_STANDARD  0x00
#define USB_SETUP_RECIPIENT_INTERFACE   0x01
#define USB_SETUP_RECIPIENT_DEVICE      0x00
#define USB_SETUP_RECIPIENT_ENDPOINT    0x02
#define USB_SETUP_RECIPIENT_OTHER       0x03

#define REQ_GET_OS_FEATURE_DESCRIPTOR 0x20
#define DESC_EXTENDED_COMPATIBLE_ID_DESCRIPTOR 0x0004
#define DESC_EXTENDED_PROPERTIES_DESCRIPTOR 0x0005

#define XINPUT_OUT_SIZE 32

#define XINPUT_DESC_TYPE_RESERVED 0x21
#define XINPUT_SECURITY_DESC_TYPE_RESERVED 0x41

static uint8_t endpoint_in = 0;
static uint8_t endpoint_out = 0;
static uint8_t xinput_out_buffer[XINPUT_OUT_SIZE] = {};
static XInputAuthData * xinputAuthData = nullptr;

// ---- Rung 2: the ISR fast path ------------------------------------------------------------
// On a button edge, the doorbell ISR calls xinput_fastpath() to rebuild buttons1/buttons2 from
// the raw pins and stamp them straight into the report already staged in USB DPRAM -- so a press
// lands in the endpoint in ~1us flat (deterministic), instead of waiting the avg 70us for the
// main loop to lap around to the read. The endpoint is kept ALWAYS-ARMED (see xfer_callback), so
// that in-place stamp is what the host reads on its very next poll.
//
// Coexistence with the loop is free: the loop's report path only writes DPRAM when its memcmp
// sees a change -- which only happens AFTER it re-reads the same pins -- so it can only ever
// write the SAME buttons the ISR already wrote. No lock, no clobber.
//
// SCOPE: the ISR handles XInput gamepad mode with digital OR analog d-pad, plus sync, debounce,
// axis-invert, 4-way, the stateless SOCD modes (neutral/up-priority) and dpad->stick -- all proven
// equal to the loop by the safety gate below. Loop-only by design: stateful SOCD (last/first-win),
// non-gamepad device types, addon-touched reports. buttons1/2 offsets are 2/3 in XInputReport.
namespace {
    volatile bool s_fastReady = false;   // snapshot done + registered (set last, single writer)
    uint8_t  s_fastEpIn = 0;
    uint32_t s_fastAllBtn = 0;           // union of every mapped pin (mask the raw read to these)
    // pin masks per XInput bit, snapshotted from the gamepad mapping at first process()
    uint32_t s_pmUp, s_pmDown, s_pmLeft, s_pmRight, s_pmStart, s_pmBack, s_pmLS, s_pmRS;
    uint32_t s_pmLB, s_pmRB, s_pmHome, s_pmA, s_pmB, s_pmX, s_pmY, s_pmL2, s_pmR2;
    // Stage 2 (combinational): axis invert + 4-way, published by the loop's gate before it compares
    // and before the ISR next runs. bool read/write is atomic on M0+, so no lock needed.
    volatile bool s_invX = false, s_invY = false, s_fourWay = false;
    // SOCD (Stage 2), stateless modes only: 0 = off/bypass, 1 = neutral, 2 = up-priority. Stateful
    // last/first-win need press-order history, so they stay gated to the loop.
    volatile uint8_t s_socd = 0;
    // Dpad mode: 0 = digital, 1 = left-analog, 2 = right-analog. Analog modes drive a stick from the
    // (fully processed) dpad -- a pure function, so the fast path can do it and the gate can prove it.
    volatile uint8_t s_dpadMode = 0;
}

// The straight pin->button map, shared by the ISR (fast path) and the loop (safety gate) so the
// two can NEVER disagree about what "straight" means -- the gate proves the ISR correct by running
// this exact function on the loop's debounced input and comparing to the loop's real report.
// always_inline so each caller gets a copy in its own section (RAM for the ISR, flash for the gate).
static inline __attribute__((always_inline))
void xinput_straight(uint32_t p, uint8_t& b1, uint8_t& b2, uint8_t& lt, uint8_t& rt,
                     int16_t& lx, int16_t& ly, int16_t& rx, int16_t& ry) {
    b1 = 0;
    if (p & s_pmUp)    b1 |= XBOX_MASK_UP;
    if (p & s_pmDown)  b1 |= XBOX_MASK_DOWN;
    if (p & s_pmLeft)  b1 |= XBOX_MASK_LEFT;
    if (p & s_pmRight) b1 |= XBOX_MASK_RIGHT;
    if (p & s_pmStart) b1 |= XBOX_MASK_START;
    if (p & s_pmBack)  b1 |= XBOX_MASK_BACK;
    if (p & s_pmLS)    b1 |= XBOX_MASK_LS;
    if (p & s_pmRS)    b1 |= XBOX_MASK_RS;
    // axis invert (Stage 2) -- swap the direction bits. The loop's process() inverts state.dpad
    // before mapping; on a straight digital map that is bit-for-bit identical to swapping here.
    if (s_invY) {   // UP <-> DOWN
        const uint8_t u = b1 & XBOX_MASK_UP, d = b1 & XBOX_MASK_DOWN;
        b1 = (b1 & ~(XBOX_MASK_UP | XBOX_MASK_DOWN)) | (u ? XBOX_MASK_DOWN : 0) | (d ? XBOX_MASK_UP : 0);
    }
    if (s_invX) {   // LEFT <-> RIGHT
        const uint8_t l = b1 & XBOX_MASK_LEFT, r = b1 & XBOX_MASK_RIGHT;
        b1 = (b1 & ~(XBOX_MASK_LEFT | XBOX_MASK_RIGHT)) | (l ? XBOX_MASK_RIGHT : 0) | (r ? XBOX_MASK_LEFT : 0);
    }
    // 4-way / 8-way filter (Stage 2). The dpad bits are 1<<0..3 in BOTH XBOX_MASK and GAMEPAD_MASK,
    // so b1's low nibble IS a logical dpad -- run the loop's own pure filterToFourWayMode() on it.
    // Same code the loop calls, so exact by construction. Order matches process(): invert, then 4-way.
    if (s_fourWay) {
        b1 = (uint8_t)((b1 & 0xF0u) | filterToFourWayMode((uint8_t)(b1 & 0x0Fu)));
    }
    // SOCD (Stage 2, AFTER 4-way -- process() order is invert, 4-way, then SOCD). Only the two
    // STATELESS modes run here; last/first-win are stateful and stay gated to the loop. This mirrors
    // socd_axis() in input/core/socd.c for neutral/up-priority exactly (dpad bits are 1<<0..3 in both
    // spaces), and the safety gate proves it equals the loop's socd_clean() output live -- so any
    // drift just disarms the fast path, never emits a wrong frame.
    if (s_socd) {
        if ((b1 & (XBOX_MASK_UP | XBOX_MASK_DOWN)) == (XBOX_MASK_UP | XBOX_MASK_DOWN)) {
            b1 &= ~(XBOX_MASK_UP | XBOX_MASK_DOWN);       // both pressed: neutral cancels...
            if (s_socd == 2u) b1 |= XBOX_MASK_UP;          // ...up-priority keeps UP
        }
        if ((b1 & (XBOX_MASK_LEFT | XBOX_MASK_RIGHT)) == (XBOX_MASK_LEFT | XBOX_MASK_RIGHT))
            b1 &= ~(XBOX_MASK_LEFT | XBOX_MASK_RIGHT);     // Left+Right cancels in both modes
    }
    b2 = 0;
    if (p & s_pmLB)   b2 |= XBOX_MASK_LB;
    if (p & s_pmRB)   b2 |= XBOX_MASK_RB;
    if (p & s_pmHome) b2 |= XBOX_MASK_HOME;
    if (p & s_pmA)    b2 |= XBOX_MASK_A;
    if (p & s_pmB)    b2 |= XBOX_MASK_B;
    if (p & s_pmX)    b2 |= XBOX_MASK_X;
    if (p & s_pmY)    b2 |= XBOX_MASK_Y;
    lt = (p & s_pmL2) ? 0xFFu : 0u;   // digital triggers; analog-trigger configs are gated out
    rt = (p & s_pmR2) ? 0xFFu : 0u;

    // Sticks. Default to CENTER using the loop's exact report conversion (state uint16 -> report
    // int16: X = v + INT16_MIN, Y = ~v + INT16_MIN). For a digital-dpad, no-analog-input config the
    // loop leaves the sticks centered, so this matches; real analog-stick inputs aren't in this map,
    // so they diverge here and the gate keeps them on the loop.
    lx = rx = (int16_t)((int16_t)GAMEPAD_JOYSTICK_MID + INT16_MIN);
    ly = ry = (int16_t)((int16_t)(~(uint16_t)GAMEPAD_JOYSTICK_MID) + INT16_MIN);
    // Analog-dpad (Stage 2, LAST -- process() converts the fully-processed dpad to a stick after
    // invert/4-way/SOCD, then zeroes the digital dpad). Same dpadToAnalog* the loop calls, so exact.
    if (s_dpadMode) {
        const uint8_t dp = (uint8_t)(b1 & 0x0Fu);
        const int16_t cx = (int16_t)((int16_t)dpadToAnalogX(dp) + INT16_MIN);
        const int16_t cy = (int16_t)((int16_t)(~dpadToAnalogY(dp)) + INT16_MIN);
        if (s_dpadMode == 1u) { lx = cx; ly = cy; }   // left-analog
        else                  { rx = cx; ry = cy; }   // right-analog
        b1 &= 0xF0u;   // dpad now lives on the stick; digital nibble -> 0 (loop's dpadOnlyMask)
    }
}

static void __not_in_flash_func(xinput_fastpath)(uint32_t committed) {
    if (!s_fastReady) return;
    uint8_t* dpram = dcd_rp2040_ep_dpram(s_fastEpIn);
    if (dpram == nullptr) return;

    // The doorbell already read the pins, applied active-low, and ran the sync window: `committed`
    // is the co-registered pressed mask. Format it straight into the staged report. (When sync is
    // off, committed == the raw pressed mask, so this is still the pure ~1us straight map.)
    uint8_t b1, b2, lt, rt;
    int16_t lx, ly, rx, ry;
    xinput_straight(committed, b1, b2, lt, rt, lx, ly, rx, ry);
    dpram[2] = b1;   // XInputReport.buttons1
    dpram[3] = b2;   // XInputReport.buttons2
    dpram[4] = lt;   // XInputReport.lt
    dpram[5] = rt;   // XInputReport.rt
    if (s_dpadMode) {   // analog-dpad: the dpad drives a stick -- write the 4 sticks (int16 LE, 2-aligned)
        *(volatile int16_t*)(dpram + 6)  = lx;   // XInputReport.lx
        *(volatile int16_t*)(dpram + 8)  = ly;   // XInputReport.ly
        *(volatile int16_t*)(dpram + 10) = rx;   // XInputReport.rx
        *(volatile int16_t*)(dpram + 12) = ry;   // XInputReport.ry
    }
#if TE_LATENCY_MEASURE
    // MEASURE (compiled out of production): stamp the device edge time (T0) into the report's reserved
    // region so a passive USB capture reads, per report, WHEN the device saw the edge -> correlate
    // against the host-receive time with no external rig. The ISR is the SOLE writer of [14..19]; the
    // loop neither diffs nor overwrites it (see process()), so there is no cross-writer race. Layout:
    //   [14] = 0x7E marker  (host discards any foreign 20-byte report on the bus)
    //   [15] = 0
    //   [16..19] = T0, little-endian uint32 (device microseconds)
    // T0 sits on the 4-aligned offset 16 so it ships as ONE 32-bit store the USB SIE can never read
    // half-updated -- a byte-by-byte write to 14..17 could tear and produce a garbage edge time.
    dpram[14] = 0x7E;
    dpram[15] = 0x00;
    *(volatile uint32_t*)(dpram + 16) = LatencyProbe::edgeTime();
    LatencyProbe::report();   // edge/deadline -> staged, in the ISR
#endif
}

// Move to Proto Enums
typedef enum
{
    XINPUT_PLED_OFF       = 0x00, // All off
    XINPUT_PLED_BLINKALL  = 0x01, // All blinking
    XINPUT_PLED_FLASH1    = 0x02, // 1 flashes, then on
    XINPUT_PLED_FLASH2    = 0x03, // 2 flashes, then on
    XINPUT_PLED_FLASH3    = 0x04, // 3 flashes, then on
    XINPUT_PLED_FLASH4    = 0x05, // 4 flashes, then on
    XINPUT_PLED_ON1       = 0x06, // 1 on
    XINPUT_PLED_ON2       = 0x07, // 2 on
    XINPUT_PLED_ON3       = 0x08, // 3 on
    XINPUT_PLED_ON4       = 0x09, // 4 on
    XINPUT_PLED_ROTATE    = 0x0A, // Rotating (e.g. 1-2-4-3)
    XINPUT_PLED_BLINK     = 0x0B, // Blinking*
    XINPUT_PLED_SLOWBLINK = 0x0C, // Slow blinking*
    XINPUT_PLED_ALTERNATE = 0x0D, // Alternating (e.g. 1+4-2+3), then back to previous*
} XInputPLEDPattern;

static void xinput_init(void) {
}

static void xinput_reset(uint8_t rhport) {
    (void)rhport;
}

static uint16_t xinput_open(uint8_t rhport, tusb_desc_interface_t const *itf_descriptor, uint16_t max_length) {
    uint16_t driver_length = 0;
    // Xbox 360 Vendor USB Interfaces: Control, Audio, Plug-in, Security
    if ( TUSB_CLASS_VENDOR_SPECIFIC == itf_descriptor->bInterfaceClass) {
        driver_length = sizeof(tusb_desc_interface_t) + (itf_descriptor->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
        TU_VERIFY(max_length >= driver_length, 0);

        tusb_desc_interface_t *p_desc = (tusb_desc_interface_t *)itf_descriptor;
        // Xbox 360 Interfaces (Control 0x01, Audio 0x02, Plug-in 0x03)
        if (itf_descriptor->bInterfaceSubClass == 0x5D &&
                ((itf_descriptor->bInterfaceProtocol == 0x01 ) ||
                (itf_descriptor->bInterfaceProtocol == 0x02 ) ||
                (itf_descriptor->bInterfaceProtocol == 0x03 )) ) {
            // Get Xbox 360 Definition
            p_desc = (tusb_desc_interface_t *)tu_desc_next(p_desc);
            TU_VERIFY(XINPUT_DESC_TYPE_RESERVED == p_desc->bDescriptorType, 0);
            driver_length += p_desc->bLength;
            p_desc = (tusb_desc_interface_t *)tu_desc_next(p_desc);
            // Control Endpoints are used for gamepad input/output
            if ( itf_descriptor->bInterfaceProtocol == 0x01 ) {
                TU_ASSERT(usbd_open_edpt_pair(rhport, (const uint8_t*)p_desc, itf_descriptor->bNumEndpoints,
                            TUSB_XFER_INTERRUPT, &endpoint_out, &endpoint_in), 0);
            }
        // Xbox 360 Security Interface
        } else if (itf_descriptor->bInterfaceSubClass == 0xFD &&
                itf_descriptor->bInterfaceProtocol == 0x13) {
            // Xinput reserved endpoint
            //-------------- Xinput Descriptor --------------//
            p_desc = (tusb_desc_interface_t *)tu_desc_next(p_desc);
            TU_VERIFY(XINPUT_SECURITY_DESC_TYPE_RESERVED == p_desc->bDescriptorType, 0);
            driver_length += p_desc->bLength;
        }
    }

    return driver_length;
}

static bool xinput_device_control_request(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;

    return true;
}

static bool xinput_control_complete(uint8_t rhport, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)request;

    return true;
}

static bool xinput_xfer_callback(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)rhport;
    (void)result;
    (void)xferred_bytes;

    if (ep_addr == endpoint_out)
        usbd_edpt_xfer(0, endpoint_out, xinput_out_buffer, XINPUT_OUT_SIZE);

    // ALWAYS-ARMED: the instant a report finishes going out, re-arm the endpoint with the report
    // STILL IN DPRAM, so it's never idle. This is what lets the ISR fast path's in-place stamp be
    // sent on the next poll instead of waiting for the loop to notice a change and re-queue. The
    // buffer we hand back IS the DPRAM (a self-copy -- harmless), so TinyUSB keeps managing the
    // DATA0/1 toggle. Paced by the host's 1ms poll, so no flood.
    else if (ep_addr == endpoint_in && tud_ready()) {
        LatencyProbe::wire();   // T2: this report just shipped on the wire -- closes edge->wire
        uint8_t* dpram = dcd_rp2040_ep_dpram(endpoint_in);
        if (dpram != nullptr && !usbd_edpt_busy(0, endpoint_in)) {
            usbd_edpt_claim(0, endpoint_in);
            usbd_edpt_xfer(0, endpoint_in, dpram, sizeof(XInputReport));
            usbd_edpt_release(0, endpoint_in);
        }
    }

    return true;
}

void XInputDriver::initialize() {
    xinputReport = {
        .report_id = 0,
        .report_size = XINPUT_ENDPOINT_SIZE,
        .buttons1 = 0,
        .buttons2 = 0,
        .lt = 0,
        .rt = 0,
        .lx = GAMEPAD_JOYSTICK_MID,
        .ly = GAMEPAD_JOYSTICK_MID,
        .rx = GAMEPAD_JOYSTICK_MID,
        .ry = GAMEPAD_JOYSTICK_MID,
        ._reserved = { },
    };

    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    deviceType = gamepadOptions.inputDeviceType;

    // controller type bindings
    if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_WHEEL) {
        // wheel
        buttonGas = new GamepadButtonMapping(0);
        buttonBrake = new GamepadButtonMapping(0);
        buttonSteerLeft = new GamepadButtonMapping(0);
        buttonSteerRight = new GamepadButtonMapping(0);
    } else if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GUITAR) {
        // guitar
        buttonFretGreen = new GamepadButtonMapping(0);
        buttonFretRed = new GamepadButtonMapping(0);
        buttonFretYellow = new GamepadButtonMapping(0);
        buttonFretBlue = new GamepadButtonMapping(0);
        buttonFretOrange = new GamepadButtonMapping(0);
        buttonFretSoloGreen = new GamepadButtonMapping(0);
        buttonFretSoloRed = new GamepadButtonMapping(0);
        buttonFretSoloYellow = new GamepadButtonMapping(0);
        buttonFretSoloBlue = new GamepadButtonMapping(0);
        buttonFretSoloOrange = new GamepadButtonMapping(0);
        buttonWhammy = new GamepadButtonMapping(0);
        buttonTilt = new GamepadButtonMapping(0);
    } else if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_DRUM) {
        // drum
        buttonDrumPadRed = new GamepadButtonMapping(0);
        buttonDrumPadBlue = new GamepadButtonMapping(0);
        buttonDrumPadYellow = new GamepadButtonMapping(0);
        buttonDrumPadGreen = new GamepadButtonMapping(0);
        buttonCymbalYellow = new GamepadButtonMapping(0);
        buttonCymbalBlue = new GamepadButtonMapping(0);
        buttonCymbalGreen = new GamepadButtonMapping(0);
        buttonKickPedalLeft = new GamepadButtonMapping(0);
        buttonKickPedalRight = new GamepadButtonMapping(0);
    } else {
        // assume gamepad if not special cased
    }

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        switch (pinMappings[pin].action) {
            case GpioAction::MODE_GUITAR_FRET_GREEN: buttonFretGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_RED: buttonFretRed->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_YELLOW: buttonFretYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_BLUE: buttonFretBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_ORANGE: buttonFretOrange->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_GREEN: buttonFretSoloGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_RED: buttonFretSoloRed->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_YELLOW: buttonFretSoloYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_BLUE: buttonFretSoloBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_FRET_SOLO_ORANGE: buttonFretSoloOrange->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_WHAMMY: buttonWhammy->pinMask |= 1 << pin; break;
            case GpioAction::MODE_GUITAR_TILT: buttonTilt->pinMask |= 1 << pin; break;

            case GpioAction::MODE_DRUM_RED_DRUMPAD: buttonDrumPadRed->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_BLUE_DRUMPAD: buttonDrumPadBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_YELLOW_DRUMPAD: buttonDrumPadYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_GREEN_DRUMPAD: buttonDrumPadGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_YELLOW_CYMBAL: buttonCymbalYellow->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_BLUE_CYMBAL: buttonCymbalBlue->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_GREEN_CYMBAL: buttonCymbalGreen->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_KICK_PEDAL_LEFT: buttonKickPedalLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_DRUM_KICK_PEDAL_RIGHT: buttonKickPedalRight->pinMask |= 1 << pin; break;

            case GpioAction::MODE_WHEEL_STEERING_LEFT: buttonSteerLeft->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_STEERING_RIGHT: buttonSteerRight->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_PEDAL_GAS: buttonGas->pinMask |= 1 << pin; break;
            case GpioAction::MODE_WHEEL_PEDAL_BRAKE: buttonBrake->pinMask |= 1 << pin; break;

            default:    break;
        }
    }

    class_driver = {
    #if CFG_TUSB_DEBUG >= 2
        .name = "XINPUT",
    #endif
        .init = xinput_init,
        .reset = xinput_reset,
        .open = xinput_open,
        .control_xfer_cb = xinput_device_control_request,
        .xfer_cb = xinput_xfer_callback,
        .sof = NULL
    };

    xAuthDriver = nullptr;
    xAuthSent = false;
}

void XInputDriver::initializeAux() {
    xAuthDriver = nullptr;
    // AUTH DRIVER NON-FUNCTIONAL FOR NOW
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    xAuthDriver = new XInputAuth(gamepadOptions.xinputAuthType);
    if ( xAuthDriver->available() ) {
        xAuthDriver->initialize();
        xinputAuthData = xAuthDriver->getAuthData();
    }
}

USBListener * XInputDriver::get_usb_auth_listener() {
    if ( xAuthDriver != nullptr && xAuthDriver->available() ) {
        return xAuthDriver->getListener();
    }
    return nullptr;
}

bool XInputDriver::getAuthSent() {
    return xAuthSent;
}

bool XInputDriver::process(Gamepad * gamepad) {
    Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;

    // One-time: snapshot the pin->button masks and arm the rung-2 ISR fast path. Deferred to here
    // (not initialize()) so the gamepad's setup() has populated the pin masks and the USB endpoint
    // is open. Fields first, then registerFastPath LAST, so the ISR never sees a half-built snapshot.
    if (!s_fastReady && endpoint_in != 0 && gamepad->mapDpadUp && gamepad->mapButtonB1) {
        s_fastEpIn = endpoint_in;
        s_pmUp    = gamepad->mapDpadUp->pinMask;    s_pmDown  = gamepad->mapDpadDown->pinMask;
        s_pmLeft  = gamepad->mapDpadLeft->pinMask;  s_pmRight = gamepad->mapDpadRight->pinMask;
        s_pmStart = gamepad->mapButtonS2->pinMask;  s_pmBack  = gamepad->mapButtonS1->pinMask;
        s_pmLS    = gamepad->mapButtonL3->pinMask;  s_pmRS    = gamepad->mapButtonR3->pinMask;
        s_pmLB    = gamepad->mapButtonL1->pinMask;  s_pmRB    = gamepad->mapButtonR1->pinMask;
        s_pmHome  = gamepad->mapButtonA1->pinMask;  s_pmA     = gamepad->mapButtonB1->pinMask;
        s_pmB     = gamepad->mapButtonB2->pinMask;  s_pmX     = gamepad->mapButtonB3->pinMask;
        s_pmY     = gamepad->mapButtonB4->pinMask;
        s_pmL2    = gamepad->mapButtonL2->pinMask;   s_pmR2   = gamepad->mapButtonR2->pinMask;
        s_fastAllBtn = s_pmUp|s_pmDown|s_pmLeft|s_pmRight|s_pmStart|s_pmBack|s_pmLS|s_pmRS
                     | s_pmLB|s_pmRB|s_pmHome|s_pmA|s_pmB|s_pmX|s_pmY|s_pmL2|s_pmR2;
        s_fastReady = true;    // snapshot published; the per-loop safety gate below arms/disarms it
    }

    xinputReport.buttons1 = 0
        | (gamepad->pressedUp()    ? XBOX_MASK_UP    : 0)
        | (gamepad->pressedDown()  ? XBOX_MASK_DOWN  : 0)
        | (gamepad->pressedLeft()  ? XBOX_MASK_LEFT  : 0)
        | (gamepad->pressedRight() ? XBOX_MASK_RIGHT : 0)
        | (gamepad->pressedS2()    ? XBOX_MASK_START : 0)
        | (gamepad->pressedS1()    ? XBOX_MASK_BACK  : 0)
        | (gamepad->pressedL3()    ? XBOX_MASK_LS    : 0)
        | (gamepad->pressedR3()    ? XBOX_MASK_RS    : 0)
    ;

    xinputReport.buttons2 = 0
        | (gamepad->pressedL1() ? XBOX_MASK_LB   : 0)
        | (gamepad->pressedR1() ? XBOX_MASK_RB   : 0)
        | (gamepad->pressedA1() ? XBOX_MASK_HOME : 0)
        | (gamepad->pressedB1() ? XBOX_MASK_A    : 0)
        | (gamepad->pressedB2() ? XBOX_MASK_B    : 0)
        | (gamepad->pressedB3() ? XBOX_MASK_X    : 0)
        | (gamepad->pressedB4() ? XBOX_MASK_Y    : 0)
    ;

    xinputReport.lx = static_cast<int16_t>(gamepad->state.lx) + INT16_MIN;
    xinputReport.ly = static_cast<int16_t>(~gamepad->state.ly) + INT16_MIN;
    xinputReport.rx = static_cast<int16_t>(gamepad->state.rx) + INT16_MIN;
    xinputReport.ry = static_cast<int16_t>(~gamepad->state.ry) + INT16_MIN;

    if (gamepad->hasAnalogTriggers)
    {
        xinputReport.lt = gamepad->pressedL2() ? 0xFF : gamepad->state.lt;
        xinputReport.rt = gamepad->pressedR2() ? 0xFF : gamepad->state.rt;
    }
    else
    {
        xinputReport.lt = gamepad->pressedL2() ? 0xFF : 0;
        xinputReport.rt = gamepad->pressedR2() ? 0xFF : 0;
    }

    // map to Xinput for special buttons
    if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_WHEEL) {
        // wheel
        if (values & buttonSteerLeft->pinMask)      { xinputReport.lx = GAMEPAD_JOYSTICK_MIN; }
        if (values & buttonSteerRight->pinMask)     { xinputReport.lx = GAMEPAD_JOYSTICK_MAX; }
        if (values & buttonBrake->pinMask)          { xinputReport.lt = INT8_MAX; }
        if (values & buttonGas->pinMask)            { xinputReport.rt = INT8_MAX; }
    } else if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GUITAR) {
        // guitar
        if (values & buttonFretGreen->pinMask)      { xinputReport.buttons2 |= XBOX_MASK_A; }
        if (values & buttonFretRed->pinMask)        { xinputReport.buttons2 |= XBOX_MASK_B; }
        if (values & buttonFretYellow->pinMask)     { xinputReport.buttons2 |= XBOX_MASK_Y; }
        if (values & buttonFretBlue->pinMask)       { xinputReport.buttons2 |= XBOX_MASK_X; }
        if (values & buttonFretOrange->pinMask)     { xinputReport.buttons2 |= XBOX_MASK_LB; }
        
        if (values & buttonFretSoloGreen->pinMask)  { xinputReport.buttons1 |= XBOX_MASK_LS; xinputReport.buttons2 |= XBOX_MASK_A; }
        if (values & buttonFretSoloRed->pinMask)    { xinputReport.buttons1 |= XBOX_MASK_LS; xinputReport.buttons2 |= XBOX_MASK_B; }
        if (values & buttonFretSoloYellow->pinMask) { xinputReport.buttons1 |= XBOX_MASK_LS; xinputReport.buttons2 |= XBOX_MASK_Y; }
        if (values & buttonFretSoloBlue->pinMask)   { xinputReport.buttons1 |= XBOX_MASK_LS; xinputReport.buttons2 |= XBOX_MASK_X; }
        if (values & buttonFretSoloOrange->pinMask) { xinputReport.buttons1 |= XBOX_MASK_LS; xinputReport.buttons2 |= XBOX_MASK_LB; }
        
        if (values & buttonWhammy->pinMask)         { xinputReport.rx = GAMEPAD_JOYSTICK_MAX; }
        if (values & buttonTilt->pinMask)           { xinputReport.ry = GAMEPAD_JOYSTICK_MAX; }
    } else if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_DRUM) {
        // drum
        if (values & buttonDrumPadRed->pinMask)     { xinputReport.buttons1 |= XBOX_MASK_RS; xinputReport.buttons2 |= XBOX_MASK_B; }
        if (values & buttonDrumPadBlue->pinMask)    { xinputReport.buttons1 |= XBOX_MASK_RS; xinputReport.buttons2 |= XBOX_MASK_X; }
        if (values & buttonDrumPadYellow->pinMask)  { xinputReport.buttons1 |= XBOX_MASK_RS; xinputReport.buttons2 |= XBOX_MASK_Y; }
        if (values & buttonDrumPadGreen->pinMask)   { xinputReport.buttons1 |= XBOX_MASK_RS; xinputReport.buttons2 |= XBOX_MASK_A; }
        if (values & buttonCymbalYellow->pinMask)   { xinputReport.buttons1 |= XBOX_MASK_UP; xinputReport.buttons2 |= XBOX_MASK_Y|XBOX_MASK_RB; }
        if (values & buttonCymbalBlue->pinMask)     { xinputReport.buttons1 |= XBOX_MASK_DOWN; xinputReport.buttons2 |= XBOX_MASK_X|XBOX_MASK_RB; }
        if (values & buttonCymbalGreen->pinMask)    { xinputReport.buttons2 |= XBOX_MASK_A|XBOX_MASK_RB; }

        if (values & buttonKickPedalLeft->pinMask)  { xinputReport.buttons2 |= XBOX_MASK_LB; }
        if (values & buttonKickPedalRight->pinMask) { xinputReport.buttons1 |= XBOX_MASK_LS; }
    } else {
        // assume gamepad if not special cased
    }

    // ---- Rung-2 SAFETY GATE: the "proven envelope" ------------------------------------------
    // The ISR fast path is a straight pin->button map -- correct ONLY where no transform sits
    // between pins and report. Rather than enumerate every transform (miss one -> silent wrong
    // reports), we PROVE equivalence live each loop: run the SAME straight map on the loop's
    // debounced input and check it equals the loop's real report, with debounce+sync disabled.
    // Match -> arm the ISR. Any divergence -- turbo/macro, stateful SOCD, non-gamepad type, an
    // addon touching the report -- disarms it and the loop owns the report.
    // Re-checked every loop, so a mode switch (hotkey/toggle) flips it safely. This gate IS the
    // harness we widen, one DST-proven stage at a time, toward "all inputs on the 1us path".
    if (s_fastReady) {
        const GamepadOptions& opt = Storage::getInstance().getGamepadOptions();
        Gamepad* gp = Storage::getInstance().GetGamepad();
        s_invX = opt.invertXAxis;   // publish transform config to the ISR BEFORE we compare or it runs
        s_invY = opt.invertYAxis;
        s_fourWay = gp->getFourWayModeActive();
        const SOCDMode socdMode = Gamepad::resolveSOCDMode(opt);
        s_socd = (socdMode == SOCD_MODE_NEUTRAL)     ? 1u
               : (socdMode == SOCD_MODE_UP_PRIORITY) ? 2u : 0u;   // 0 also covers bypass (no-op)
        const DpadMode dpadMode = gp->getActiveDpadMode();
        s_dpadMode = (dpadMode == DpadMode::DPAD_MODE_LEFT_ANALOG)  ? 1u
                   : (dpadMode == DpadMode::DPAD_MODE_RIGHT_ANALOG) ? 2u : 0u;   // 0 = digital
        uint8_t o1, o2, olt, ort;
        int16_t olx, oly, orx, ory;
        xinput_straight(gp->debouncedGpio, o1, o2, olt, ort, olx, oly, orx, ory);
        const bool mappingStraight = (xinputReport.buttons1 == o1) && (xinputReport.buttons2 == o2)
                                   && (xinputReport.lt == olt) && (xinputReport.rt == ort)
                                   && (xinputReport.lx == olx) && (xinputReport.ly == oly)
                                   && (xinputReport.rx == orx) && (xinputReport.ry == ory);

        // PRODUCTION SAFETY -- PROACTIVE whitelist. Arm the ISR ONLY inside the config envelope it
        // provably covers, so a partially-covered mode can never emit even ONE wrong frame before the
        // reactive mappingStraight check would notice. Anything outside falls back to the loop = the
        // incumbent GP2040 path (known-good). The ISR now handles: invert, sync, debounce, 4-way, the
        // STATELESS SOCD modes (neutral/up-priority), and analog-dpad (digital + both analog modes).
        // Kept on the loop by DESIGN, not TODO: STATEFUL SOCD (last/first-win depend on press-order
        // history, which the edge-driven ISR and the snapshot-driven loop can't agree on), non-gamepad
        // device types (wheel/guitar/drum -> different report formats), and addon-touched reports
        // (addons run in the loop). mappingStraight (buttons + triggers + sticks) is the final backstop.
        const bool gamepadType = (deviceType != InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_WHEEL)
                              && (deviceType != InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GUITAR)
                              && (deviceType != InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_DRUM);
        const bool dpadModeOk = (dpadMode == DpadMode::DPAD_MODE_DIGITAL)
                             || (dpadMode == DpadMode::DPAD_MODE_LEFT_ANALOG)
                             || (dpadMode == DpadMode::DPAD_MODE_RIGHT_ANALOG);
        const bool socdStateless = (socdMode == SOCD_MODE_BYPASS)
                                || (socdMode == SOCD_MODE_NEUTRAL)
                                || (socdMode == SOCD_MODE_UP_PRIORITY);
        const bool eligible = mappingStraight && gamepadType
                           && dpadModeOk && socdStateless && !g_teAddonTouchedInputs;
        GpioDoorbell::registerFastPath(eligible ? &xinput_fastpath : nullptr);
    }

    bool reportSent = false;

    // HOT BUFFER: whenever the report changes, get it to the endpoint IMMEDIATELY -- queue if
    // the IN endpoint is free, else overwrite the report already staged in DPRAM in place, so
    // the next host poll reads the FRESHEST state instead of the stale one it was holding.
    // Neither path waits for the endpoint to free. The probe now times raw edge -> endpoint.
    // Diff and freshen on the report CONTENT only (bytes 0..13). The reserved region [14..19] is not
    // report state -- treating it as content would re-send on every change to it and let the loop
    // clobber it. In measurement builds that region is the ISR's edge-time stamp, so content-only
    // keeps the two writers from racing; in production it is simply unused.
    if ( memcmp(last_report, &xinputReport, offsetof(XInputReport, _reserved)) != 0) {
        if ( tud_ready() && (endpoint_in != 0) && (!usbd_edpt_busy(0, endpoint_in)) ) {
#if TE_LATENCY_MEASURE
            // Rare path (endpoint idle): TinyUSB copies the whole 20-byte buffer, so carry the ISR's
            // live stamp forward rather than shipping this RAM copy's stale reserved bytes.
            uint8_t* dpram = dcd_rp2040_ep_dpram(endpoint_in);
            if (dpram != nullptr)
                memcpy(xinputReport._reserved, dpram + offsetof(XInputReport, _reserved),
                       sizeof(xinputReport._reserved));
#endif
            usbd_edpt_claim(0, endpoint_in);
            usbd_edpt_xfer(0, endpoint_in, (uint8_t *)&xinputReport, sizeof(XInputReport));
            usbd_edpt_release(0, endpoint_in);
            memcpy(last_report, &xinputReport, sizeof(XInputReport));
            LatencyProbe::report();
            reportSent = true;
        } else if ( tud_ready() && (endpoint_in != 0) ) {
            uint8_t* dpram = dcd_rp2040_ep_dpram(endpoint_in);   // report already staged for the SIE
            if (dpram != nullptr) {
                // Freshen CONTENT in place; leave [14..19] to the ISR so its edge stamp survives.
                memcpy(dpram, &xinputReport, offsetof(XInputReport, _reserved));
                memcpy(last_report, &xinputReport, sizeof(XInputReport));
                LatencyProbe::report();
                reportSent = true;
            }
        }
    }

    // clear potential initial uncaught data in endpoint_out from before registration of xfer_cb
    if (tud_ready() &&
        (endpoint_out != 0) && (!usbd_edpt_busy(0, endpoint_out)))
    {
        usbd_edpt_claim(0, endpoint_out);									 // Take control of OUT endpoint
        usbd_edpt_xfer(0, endpoint_out, xinput_out_buffer, XINPUT_OUT_SIZE); 		 // Retrieve report buffer
        usbd_edpt_release(0, endpoint_out);									 // Release control of OUT endpoint
    }

    //---------------
    if (memcmp(xinput_out_buffer, featureBuffer, XINPUT_OUT_SIZE) != 0) { // check if new write to xinput_out_buffer from xinput_xfer_callback
        memcpy(featureBuffer, xinput_out_buffer, XINPUT_OUT_SIZE);
        switch (featureBuffer[0]) {
            case 0x00:
                if (featureBuffer[1] == 0x08) {
                    if (processedGamepad->auxState.haptics.leftActuator.enabled) {
                        processedGamepad->auxState.haptics.leftActuator.active = (featureBuffer[3] > 0);
                        processedGamepad->auxState.haptics.leftActuator.intensity = featureBuffer[3];
                    }
                    if (processedGamepad->auxState.haptics.rightActuator.enabled) {
                        processedGamepad->auxState.haptics.rightActuator.active = (featureBuffer[4] > 0);
                        processedGamepad->auxState.haptics.rightActuator.intensity = featureBuffer[4];
                    }
                }
                break;
            case 0x01:
                // Player LED
                if (featureBuffer[1] == 0x03) {
                    // determine the player ID based on LED status
                    processedGamepad->auxState.playerID.active = true;
                    processedGamepad->auxState.playerID.ledValue = featureBuffer[2];

                    if ( featureBuffer[2] == XINPUT_PLED_ON1 ) {
                        processedGamepad->auxState.playerID.value = 1;
                    } else if ( featureBuffer[2] == XINPUT_PLED_ON2 ) {
                        processedGamepad->auxState.playerID.value = 2;
                    } else if ( featureBuffer[2] == XINPUT_PLED_ON3 ) {
                        processedGamepad->auxState.playerID.value = 3;
                    } else if ( featureBuffer[2] == XINPUT_PLED_ON4 ) {
                        processedGamepad->auxState.playerID.value = 4;
                    } else {
                        processedGamepad->auxState.playerID.value = 0;
                    }
                }
                break;
        }
    }

    return reportSent;
}

void XInputDriver::processAux() {
    if ( xAuthDriver != nullptr && xAuthDriver->available() ) {
        xAuthDriver->process();
    }
}

// tud_hid_get_report_cb
uint16_t XInputDriver::get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    memcpy(buffer, &xinputReport, sizeof(XInputReport));
    return sizeof(XInputReport);
}

// Only respond to vendor control xfers if we have a mounted x360 device
bool XInputDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  // Do nothing if we have no auth driver
    if ( xAuthDriver == nullptr || !xAuthDriver->available() ) {
        return false;
    }

    uint16_t len = 0;
    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        // Write IN data on control_stage_setup only
        if (stage == CONTROL_STAGE_SETUP) {
            uint16_t state = 1; // 1 = in-progress, 2 = complete
            switch (request->bRequest) {
                    case XSM360_GET_SERIAL:
                        // Stall if we don't have a dongle ready
                        if ( xinputAuthData->dongle_ready == false ) {
                            return false;
                        }
                        len = X360_AUTHLEN_DONGLE_SERIAL;
                        memcpy(tud_buffer, xinputAuthData->dongleSerial, len);
                        xAuthSent = true; // triggers on serial request but this is only for visual flair
                        break;
                    case XSM360_RESPOND_CHALLENGE:
                        if ( xinputAuthData->xinputState == GPAuthState::send_auth_dongle_to_console ) {
                            memcpy(tud_buffer, xinputAuthData->passthruBuffer, xinputAuthData->passthruBufferLen);
                            len = xinputAuthData->passthruBufferLen;
                        } else {
                            // Stall if we don't have a dongle ready
                            return false;
                        }
                        break;
                    case XSM360_AUTH_KEEPALIVE:
                        len = 0;
                        break;
                    case XSM360_REQUEST_STATE:
                        // State Ready = 2, Not-Ready = 1
                        if ( xinputAuthData->xinputState == GPAuthState::send_auth_dongle_to_console ) {
                            state = 2;
                        } else {
                            state = 1;
                        }
                        memcpy(tud_buffer, &state, sizeof(state));
                        len = sizeof(state);
                        break;
                    default:
                        break;
            };
            tud_control_xfer(rhport, request, tud_buffer, len);
        }
    } else if (request->bmRequestType_bit.direction == TUSB_DIR_OUT) {
        if (stage == CONTROL_STAGE_SETUP ) { // Pass on output setup in DIR OUT stage
            tud_control_xfer(rhport, request, tud_buffer, request->wLength);
        } else if ( stage == CONTROL_STAGE_DATA ) {
            // Buf is filled, we can save the data to our auth
            switch (request->bRequest) {
                    case XSM360AuthRequest::XSM360_INIT_AUTH:
                        if ( xinputAuthData->xinputState == GPAuthState::auth_idle_state ) {
                            memcpy(xinputAuthData->passthruBuffer, tud_buffer, request->wLength);
                            xinputAuthData->passthruBufferLen = request->wLength;
                            xinputAuthData->passthruBufferID = XSM360AuthRequest::XSM360_INIT_AUTH;
                            xinputAuthData->xinputState = GPAuthState::send_auth_console_to_dongle;
                        }
                        break;
                    case XSM360AuthRequest::XSM360_VERIFY_AUTH:
                        memcpy(xinputAuthData->passthruBuffer, tud_buffer, request->wLength);
                        xinputAuthData->passthruBufferLen = request->wLength;
                        xinputAuthData->passthruBufferID = XSM360AuthRequest::XSM360_VERIFY_AUTH;
                        xinputAuthData->xinputState = GPAuthState::send_auth_console_to_dongle;
                        break;
                    default:
                        break;
            };
        }
    }

    return true;
}

const uint16_t * XInputDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
    char *value;
    // Check for override settings
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    if ( gamepadOptions.usbDescOverride == true ) {
        switch(index) {
            case 1:
                value = gamepadOptions.usbDescManufacturer;
                break;
            case 2:
                value = gamepadOptions.usbDescProduct;
                break;
            case 3:
                value = gamepadOptions.usbDescVersion;
            default:
                value = (char *)xinput_get_string_descriptor(index);
                break;
        }
    } else {
        value = (char *)xinput_get_string_descriptor(index);
    }
    return getStringDescriptor((const char*)value, index); // getStringDescriptor returns a static array
}

const uint8_t * XInputDriver::get_descriptor_device_cb() {
    // Check for override settings
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    if ( gamepadOptions.usbOverrideID == true ) {
        static uint8_t modified_device_descriptor[18];
        memcpy(modified_device_descriptor, xinput_device_descriptor, sizeof(xinput_device_descriptor));
        memcpy(&modified_device_descriptor[8], (uint8_t*)&gamepadOptions.usbVendorID, sizeof(uint16_t)); // Vendor ID
        memcpy(&modified_device_descriptor[10], (uint8_t*)&gamepadOptions.usbProductID, sizeof(uint16_t)); // Product ID
        return (const uint8_t*)modified_device_descriptor;
    }
    return xinput_device_descriptor;
}

const uint8_t * XInputDriver::get_hid_descriptor_report_cb(uint8_t itf) {
    return nullptr;
}

const uint8_t * XInputDriver::get_descriptor_configuration_cb(uint8_t index) {
    uint16_t configDescriptorSize = sizeof(xinput_configuration_descriptor);
    memcpy(configDescriptor, &xinput_configuration_descriptor, configDescriptorSize);

    // check subtype
    GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
    deviceType = gamepadOptions.inputDeviceType;
    if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_WHEEL) {
        // wheel
        configDescriptor[22] = XInputSubtype::XINPUT_SUBTYPE_WHEEL;
    } else if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_GUITAR) {
        // guitar
        configDescriptor[22] = XInputSubtype::XINPUT_SUBTYPE_GUITAR;
    } else if (deviceType == InputModeDeviceType::INPUT_MODE_DEVICE_TYPE_DRUM) {
        // drum
        configDescriptor[22] = XInputSubtype::XINPUT_SUBTYPE_DRUMS;
    } else {
        // assume gamepad if not special cased
        configDescriptor[22] = XInputSubtype::XINPUT_SUBTYPE_GAMEPAD;
    }

    return configDescriptor;
}

const uint8_t * XInputDriver::get_descriptor_device_qualifier_cb() {
    return nullptr;
}

uint16_t XInputDriver::GetJoystickMidValue() {
    return GAMEPAD_JOYSTICK_MID;
}
