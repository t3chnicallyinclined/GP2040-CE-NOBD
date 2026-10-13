/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 Open Stick Community (gp2040-ce.info)
 */

#ifndef _USBDRIVER_CPP_
#define _USBDRIVER_CPP_

#include <string.h>
#include "tusb.h"
#include "drivermanager.h"
#include "storagemanager.h"
#include "hardware/structs/usb_dpram.h"

// NOBD hot-buffer hook: the DPRAM data buffer the SIE is serving for an endpoint, so the report
// already staged there can be overwritten in place (a new press updates the pending report instead
// of waiting to re-queue). Read straight from the endpoint control register, where TinyUSB's
// rp2040 dcd records the buffer offset when it opens the endpoint -- no TinyUSB patch needed.
// NULL if the endpoint is not open (EP0 or a cleared control word, e.g. after a bus reset).
extern "C" uint8_t* __not_in_flash_func(dcd_rp2040_ep_dpram)(uint8_t ep_addr) {
	const uint8_t epnum = ep_addr & 0x0Fu;
	if (epnum == 0) return nullptr;
	const uint32_t ctrl = (ep_addr & 0x80u) ? usb_dpram->ep_ctrl[epnum - 1].in
	                                         : usb_dpram->ep_ctrl[epnum - 1].out;
	if (!(ctrl & EP_CTRL_ENABLE_BITS)) return nullptr;
	return (uint8_t*)usb_dpram + (ctrl & 0xFFC0u);
}

static bool usb_mounted;
static bool usb_suspended;

bool get_usb_mounted(void) {
	return usb_mounted;
}

bool get_usb_suspended(void) {
	return usb_suspended;
}

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
	*driver_count = 1;
	return DriverManager::getInstance().getDriver()->get_class_driver();
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
	return DriverManager::getInstance().getDriver()->get_report(report_id, report_type, buffer, reqlen);
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
	DriverManager::getInstance().getDriver()->set_report(report_id, report_type, buffer, bufsize);
}

// Invoked when device is mounted
void tud_mount_cb(void)
{
	usb_mounted = true;
	usb_suspended = false;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	usb_mounted = false;
	usb_suspended = false;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
	(void)remote_wakeup_en;
	usb_suspended = true;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
	usb_suspended = false;
}

// Vendor Controlled XFER occured
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
	return DriverManager::getInstance().getDriver()->vendor_control_xfer_cb(rhport, stage, request);
}


// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	return DriverManager::getInstance().getDriver()->get_descriptor_string_cb(index, langid);
}

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb() {
	return DriverManager::getInstance().getDriver()->get_descriptor_device_cb();
}

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
	return DriverManager::getInstance().getDriver()->get_hid_descriptor_report_cb(itf);
}

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
// User-selectable polling rate: rewrite bInterval on the interrupt IN endpoints a driver
// declares at 1 ms (the report endpoints) to the configured interval. Endpoints a driver
// already runs slower (auth, audio, chatpad, ...) are left alone, and so is web config.
// Descriptor time only -- nothing here runs on the input path.
static uint8_t const *apply_poll_interval(uint8_t const *desc) {
	static uint8_t patched[512];
	const uint32_t interval = Storage::getInstance().getGamepadOptions().usbPollInterval;
	if (desc == nullptr || interval <= 1 || DriverManager::getInstance().isConfigMode())
		return desc;

	const uint16_t total = tu_le16toh(tu_unaligned_read16(desc + 2));
	if (total > sizeof(patched))
		return desc;
	memcpy(patched, desc, total);

	for (uint16_t i = 0; i + 1 < total && patched[i] != 0; i += patched[i]) {
		uint8_t *d = &patched[i];
		if (d[1] == TUSB_DESC_ENDPOINT && d[0] >= 7 &&
		    (d[2] & TUSB_DIR_IN_MASK) &&
		    (d[3] & 0x03) == TUSB_XFER_INTERRUPT &&
		    d[6] == 1) {
			d[6] = (uint8_t)interval;
		}
	}
	return patched;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
	return apply_poll_interval(DriverManager::getInstance().getDriver()->get_descriptor_configuration_cb(index));
}

uint8_t const* tud_descriptor_device_qualifier_cb() {
	return DriverManager::getInstance().getDriver()->get_descriptor_device_qualifier_cb();
}

// Invoked when received GET OTHER SPEED CONFIGURATION DESCRIPTOR request
uint8_t const* tud_descriptor_other_speed_configuration_cb(uint8_t index) {
	return DriverManager::getInstance().getDriver()->get_descriptor_other_speed_configuration_cb(index);
}

#endif
