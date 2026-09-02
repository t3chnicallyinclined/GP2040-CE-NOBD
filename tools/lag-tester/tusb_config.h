/*
 * TinyUSB config for the lag-tester: DEVICE on the native USB (CDC, prints results to the PC) +
 * HOST on the MAX3421E (reads the DUT). No hardware contention -- the host is the external SPI
 * chip, so the RP2040's own USB is free for the results serial port.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU                 OPT_MCU_RP2040
#define CFG_TUSB_OS                  OPT_OS_PICO
#define CFG_TUSB_DEBUG               0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN           __attribute__ ((aligned(4)))

// DEVICE off: the SDK's stdio_usb disables itself when tinyusb_host is linked, so results go out
// over UART instead (see CMakeLists / README). Host-only keeps the USB stack simple.
#define CFG_TUD_ENABLED              0

//--------------------------------------------------------------------
// HOST (MAX3421E over SPI) -- enumerate + poll the DUT as an XInput controller
//--------------------------------------------------------------------
#define CFG_TUH_ENABLED              1
#define CFG_TUH_MAX3421              1
#define BOARD_TUH_RHPORT             1          // host = rhport 1 (native device is rhport 0)
#define CFG_TUH_MAX3421_ENDPOINT_TOTAL 8

#define CFG_TUH_DEVICE_MAX           1          // one DUT, directly attached
#define CFG_TUH_ENUMERATION_BUFSIZE  256
#define CFG_TUH_HUB                  0          // DUT plugs straight into the FeatherWing jack

// custom XInput host class driver (registered via usbh_app_driver_get_cb in main.cpp)
#define CFG_TUH_XINPUT               1
#define CFG_TUH_XINPUT_EPIN_BUFSIZE  64
#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64

#endif // _TUSB_CONFIG_H_
