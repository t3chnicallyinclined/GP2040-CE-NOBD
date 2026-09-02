/*
 * TinyUSB config for the Track-B bulk-probe: a single WinUSB vendor interface with a bulk IN
 * endpoint. Windows auto-binds WinUSB (no driver install) via the MS OS 2.0 descriptor, and a
 * user-space app tight-polls the bulk IN to measure how fast we can pull data off the device --
 * the make-or-break question for beating the 1 kHz USB-FS interrupt-poll floor.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU                 OPT_MCU_RP2040
#define CFG_TUSB_OS                  OPT_OS_PICO
#define CFG_TUSB_DEBUG               0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN           __attribute__ ((aligned(4)))

#define CFG_TUD_ENABLED              1
#define BOARD_TUD_RHPORT             0
#define CFG_TUD_ENDPOINT0_SIZE       64

// One vendor (WinUSB) interface; no CDC/HID/MSC.
#define CFG_TUD_VENDOR               1
#define CFG_TUD_VENDOR_RX_BUFSIZE    64
#define CFG_TUD_VENDOR_TX_BUFSIZE    64

#endif // _TUSB_CONFIG_H_
