#ifndef BULK_PROBE_USB_DESCRIPTORS_H
#define BULK_PROBE_USB_DESCRIPTORS_H

#include <stdint.h>

// Vendor control-request codes referenced by the BOS descriptor.
enum {
    VENDOR_REQUEST_MICROSOFT = 2,
};

// The MS OS 2.0 descriptor (defined in usb_descriptors.c) -- served on the Microsoft vendor request
// so Windows binds WinUSB to our vendor interface with no driver install.
extern uint8_t const desc_ms_os_20[];

#endif // BULK_PROBE_USB_DESCRIPTORS_H
