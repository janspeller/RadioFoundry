// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AnyTone AT-D878UVII PLUS: board descriptor, clocks and reset-time pin levels, key map
// (docs/spec/hardware-d878uv2.md)
//
// What this board does **not** carry yet: the Bluetooth/APRS daughter board, whose protocol over UART4 is only
// partly recorded (hardware-d878uv2.md 4.4).
#include "board.h"
#include "config.h"
#include "d878uv2.h"
#include "pins.h"
#include "mcu_io.h"
#include "hal_time.h"
#if CONFIG_USB
#include "usb/usb_device.h"
#endif
#if CONFIG_DMR
#include "modem/sct/sct_dmr.h"
#endif
#if CONFIG_GPS
#include "gps_uart/gps_uart.h"
#endif

static const band_t bands[] = {
	{ 136000000, 174000000, true },
	{ 400000000, 480000000, true },
#if CONFIG_BCAST_FM
	{ 87500000, 108000000, false },		// broadcast FM on the RDA5802 (hardware-d878uv2.md 4.1)
#endif
};

// Four levels, as the factory calibration carries four transmit power tables per band
// (docs/spec/hardware-d878uv2.md 5). The watts each gives are **not recorded**: the labels say which step is which
// until a power meter on the bench measures them (docs/testing/d878uv2-bringup.md, stage 7)
static const char *const power_names[] = { "L1", "L2", "L3", "L4" };

// The front keys (docs/spec/hardware-d878uv2.md 4.1.1). MENU confirms (held: the key lock, the core's long
// QUICK) and EXIT goes back (held: Home). The arrows step the channel on Home and move a list elsewhere. P1 and P2
// sit beside them on the matrix; PF1 and PF2 are the side keys under PTT and PF3 is the key on top. All five
// programmable keys are reassignable in Settings > Keys and the codeplug, named as the radio prints them, in the
// order the stock firmware's own menu lists them.
#if CONFIG_DMR_EMERGENCY
#define PF3_LONG	ACT_EMERGENCY
#else
#define PF3_LONG	ACT_POWER
#endif
static const keymap_entry_t keymap[] = {
	{ KEY_MENU, ACT_SELECT, ACT_QUICK, false, false, 0 },
	{ KEY_EXIT, ACT_BACK, ACT_BACK, false, false, 0 },
	{ KEY_UP, ACT_NAV_UP, ACT_NAV_UP, false, true, 0 },
	{ KEY_DOWN, ACT_NAV_DOWN, ACT_NAV_DOWN, false, true, 0 },
	{ KEY_0, ACT_DIGIT_0, ACT_DIGIT_0, false, false, 0 },
	{ KEY_1, ACT_DIGIT_1, ACT_DIGIT_1, false, false, 0 },
	{ KEY_2, ACT_DIGIT_2, ACT_DIGIT_2, false, false, 0 },
	{ KEY_3, ACT_DIGIT_3, ACT_DIGIT_3, false, false, 0 },
	{ KEY_4, ACT_DIGIT_4, ACT_DIGIT_4, false, false, 0 },
	{ KEY_5, ACT_DIGIT_5, ACT_DIGIT_5, false, false, 0 },
	{ KEY_6, ACT_DIGIT_6, ACT_DIGIT_6, false, false, 0 },
	{ KEY_7, ACT_DIGIT_7, ACT_DIGIT_7, false, false, 0 },
	{ KEY_8, ACT_DIGIT_8, ACT_DIGIT_8, false, false, 0 },
	{ KEY_9, ACT_DIGIT_9, ACT_DIGIT_9, false, false, 0 },
	{ KEY_STAR, ACT_QUICK, ACT_QUICK, false, false, '*' },
	{ KEY_HASH, ACT_CONTEXT, ACT_CONTEXT, false, false, '#' },
	{ KEY_P1, ACT_QUICK, ACT_ZONE_UP, false, false, 0 },
	{ KEY_P2, ACT_CONTEXT, ACT_CONTEXT, false, false, 0 },
	{ KEY_PF1, ACT_MONITOR, ACT_MONITOR, true, false, 0 },
	{ KEY_PF2, ACT_AB, ACT_AB, false, false, 0 },
	{ KEY_PF3, ACT_POWER, PF3_LONG, false, false, 0 },
	{ KEY_PTT, ACT_PTT, ACT_PTT, true, false, 0 },
};

// Reassignable in Settings > Keys and the codeplug (core/input/keyassign.h), named as printed
static const uint8_t side_keys[] = { KEY_PF1, KEY_PF2, KEY_PF3, KEY_P1, KEY_P2 };
static const char *const side_key_names[] = { "PF1", "PF2", "PF3", "P1", "P2" };

const board_t board = {
	.name = BOARD_T_NAME,
#if CONFIG_DMR
	.caps = CAP_KEYPAD | CAP_ENCODER | CAP_DMR | CAP_MODEM_VOICE | (CONFIG_GPS ? CAP_GPS : 0) | (CONFIG_BCAST_FM ? CAP_BCAST_FM : 0),
	.dmr = &sct_dmr_modem,	// start() fails until the baseband has booted: FM only without the boot block
	.modem_codec = d878uv2_modem_codec,
	.boot_busy = d878uv2_modem_booting,
#else
	.caps = CAP_KEYPAD | CAP_ENCODER | (CONFIG_GPS ? CAP_GPS : 0) | (CONFIG_BCAST_FM ? CAP_BCAST_FM : 0),
#endif
#if CONFIG_GPS
	.gps_enable = mcu_gps_enable,
	.gps_present = mcu_gps_present,	// CAP_GPS: say so only once the module has been heard
#endif
	.bands = bands,
	.band_count = sizeof(bands) / sizeof(bands[0]),
	.power_count = sizeof(power_names) / sizeof(power_names[0]),
	.power_names = power_names,
	.power_default = 0,
	.display = &d878uv2_display,
	.input = &d878uv2_input,
	.keymap = keymap,
	.keymap_len = sizeof(keymap) / sizeof(keymap[0]),
	.side_keys = side_keys,
	.side_key_names = side_key_names,
	.side_key_count = sizeof(side_keys),
	.quick_label = "P1",
	.context_label = "P2",
	.lock_label = "MENU",
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
#if CONFIG_DIAG
	.diag_line = d878uv2_diag_line,
	.diag_reset = d878uv2_diag_reset,
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
	// The factory calibration: read before anything can transmit, never written (hardware-d878uv2.md 5)
	d878uv2_cal_load();
#if CONFIG_DMR
	d878uv2_modem_init();
#endif
#if CONFIG_GPS
	d878uv2_gps_init();
#endif
}
