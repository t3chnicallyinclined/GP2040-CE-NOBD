/*
 * Track-B bulk-probe descriptors: ONE WinUSB vendor interface with a bulk IN endpoint.
 * Adapted from TinyUSB's webusb_serial example, stripped to vendor-only + MS OS 2.0 (no CDC,
 * no WebUSB, no bsp). The MS OS 2.0 descriptor makes Windows bind WinUSB with no driver install.
 */
#include <string.h>
#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// Device descriptor
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0210,          // 2.1 so the host reads our BOS (MS OS 2.0 lives there)
    .bDeviceClass       = 0x00,            // per-interface (the vendor interface is class 0xFF)
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCAFE,
    .idProduct          = 0x4020,          // Track-B bulk probe
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void) { return (uint8_t const*)&desc_device; }

//--------------------------------------------------------------------+
// Configuration descriptor
//--------------------------------------------------------------------+
enum { ITF_NUM_VENDOR = 0, ITF_NUM_TOTAL };

#define EPNUM_VENDOR_OUT  0x01
#define EPNUM_VENDOR_IN   0x81
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // interface number, string index, EP OUT, EP IN, EP size (FS bulk = 64)
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 4, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64)
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_configuration; }

//--------------------------------------------------------------------+
// BOS + MS OS 2.0 (WinUSB auto-bind)
//--------------------------------------------------------------------+
#define BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN 0xB2

uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

uint8_t const* tud_descriptor_bos_cb(void) { return desc_bos; }

uint8_t const desc_ms_os_20[] = {
    // Set header: length, type, windows version, total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    // Configuration subset header
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A),
    // Function subset header (our vendor interface)
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A-0x08),
    // Compatible ID: WINUSB
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W','I','N','U','S','B',0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // Registry property: DeviceInterfaceGUIDs = {975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN-0x0A-0x08-0x08-0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,'I',0,'n',0,'t',0,'e',0,
    'r',0,'f',0,'a',0,'c',0,'e',0,'G',0,'U',0,'I',0,'D',0,'s',0,0,0,
    U16_TO_U8S_LE(0x0050),
    '{',0,'9',0,'7',0,'5',0,'F',0,'4',0,'4',0,'D',0,'9',0,'-',0,
    '0',0,'D',0,'0',0,'8',0,'-',0,'4',0,'3',0,'F',0,'D',0,'-',0,
    '8',0,'B',0,'3',0,'E',0,'-',0,'1',0,'2',0,'7',0,'C',0,'A',0,
    '8',0,'A',0,'F',0,'F',0,'F',0,'9',0,'D',0,'}',0,0,0,0,0
};
TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "MS OS 2.0 size mismatch");

//--------------------------------------------------------------------+
// String descriptors
//--------------------------------------------------------------------+
static char const* string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },   // 0: English (0x0409)
    "NOBD",                          // 1: Manufacturer
    "Track-B Bulk Probe",            // 2: Product
    "TRACKB-P0-0001",                // 3: Serial
    "Bulk stream",                   // 4: Vendor interface
};

static uint16_t _desc_str[32 + 1];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        size_t const max = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
        if (chr_count > max) chr_count = max;
        for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
