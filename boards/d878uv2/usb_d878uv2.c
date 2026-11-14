// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS USB identity: the IDs every RadioFoundry radio enumerates with and the chip's unique ID
// (platform.h)
#include "config.h"
#if CONFIG_USB	// the whole file
#include "mcu_io.h"
#include "pins.h"
#include "usb/usb_port.h"

const usb_ids_t board_usb_ids = {
	.vid = MCU_USB_VID,
	.pid_serial = MCU_USB_PID_SERIAL,
	.pid_network = MCU_USB_PID_NETWORK,
	.bcd_device = 0x0100,
	.manufacturer = "RadioFoundry",
	.product = "AT-D878UVII",
	.serial_interface = "AT-D878UVII serial",
	.serial_number = mcu_usb_serial_number,
};

#ifdef GD32_USB_VID
// The GD32's USBD core has no pull-up of its own: PE2 switches the board's, driven high when the stack attaches
// and low when it detaches (docs/spec/hardware-d878uv2.md 4.1)
void board_usb_pullup(bool on)
{
	mcu_pin_out(PIN_USB_ATTACH, on, false);
}
#endif

#endif	// CONFIG_USB
