// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AnyTone AT-D878UVII PLUS: board descriptor, clocks and reset-time pin levels, key map
// (docs/spec/hardware-d878uv2.md)
//
// What this board does **not** carry yet, so its switches are off in config.mk: the DMR baseband's glue, the GPS
// (whose module is enabled from the RF chip's gpio2) and the Bluetooth/APRS daughter board. The radio comes up as
// an FM handheld, which is what the first bring-up stages need (docs/testing/d878uv2-bringup.md).
#include "board.h"
#include "config.h"
#include "d878uv2.h"
#include "pins.h"
#include "mcu_io.h"
#include "hal_time.h"
#if CONFIG_USB
#include "usb/usb_device.h"
#endif

static const band_t bands[] = {
	{ 136000000, 174000000, true },
	{ 400000000, 480000000, true },
#if CONFIG_BCAST_FM
	{ 87500000, 108000000, false },		// broadcast FM on the RDA5802 (hardware-d878uv2.md 4.1)
#endif
};

// The rated power per level is **not recorded**: the labels say which step is which until a power meter on the
// bench gives the watts (docs/testing/d878uv2-bringup.md, stage 7). The drive values behind them are placeholders
// in radio_d878uv2.c, deliberately low
static const char *const power_names[] = { "Low", "Mid", "High" };

// Only PTT is mapped. It is a direct input on PA10, not a matrix position, so it works before the keypad does;
// every matrix key waits for a bench session to fill in the map (input_d878uv2.c, hardware-d878uv2.md 4.3)
static const keymap_entry_t keymap[] = {
	{ KEY_PTT, ACT_PTT, ACT_PTT, true, false, 0 },
};

const board_t board = {
	.name = BOARD_T_NAME,
	.caps = CAP_KEYPAD | CAP_ENCODER | (CONFIG_BCAST_FM ? CAP_BCAST_FM : 0),
	.bands = bands,
	.band_count = sizeof(bands) / sizeof(bands[0]),
	.power_count = sizeof(power_names) / sizeof(power_names[0]),
	.power_names = power_names,
	.power_default = 0,
	.display = &d878uv2_display,
	.input = &d878uv2_input,
	.keymap = keymap,
	.keymap_len = sizeof(keymap) / sizeof(keymap[0]),
	.radio = &d878uv2_radio,
	.power = &d878uv2_power,
	.battery_low_mv = 6100,		// the stock firmware's own warning levels (hardware-d878uv2.md 4.3)
	.battery_critical_mv = 6000,
#if CONFIG_USB
	.host_port = &usb_cdc_stream,	// the debug link and the CPS on the USB serial port
#endif
	.system = &mcu_system,
#if CONFIG_NAND_STORAGE
	.flash = &d878uv2_flash,
#endif
};

// The 12 MHz crystal to 120 MHz, as every GD32F303 board in this family runs
#define CRYSTAL_HZ	12000000u
#define CORE_HZ		120000000u

void board_rf_safe(void)
{
	d878uv2_rf_safe();
}

void board_early_init(void)
{
	mcu_clock_init(CRYSTAL_HZ, CORE_HZ);
	// PA15, PB3 and PB4 are the NAND's MISO and the RF bus: free them from JTAG, keep SWD
	mcu_jtag_release();
}

void board_init(void)
{
	// The power hold first: everything else runs on it
	mcu_pin_out(PIN_POWER_HOLD, true, false);
	hal_delay_ms(10);
	d878uv2_nand_init();
#if CONFIG_NAND_STORAGE
	// The core's storage, mounted before storage_init reads it. Without it the radio runs on the default settings
	d878uv2_flash_init();
#endif
}
