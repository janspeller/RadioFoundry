// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Radtel RT-4D: board descriptor, reset-time pin levels, key map, data flash map (docs/spec/hardware-rt4d.md)
#include "board.h"
#include "config.h"
#include "rt4d.h"
#include "pins.h"
#include "platform.h"

static const band_t bands[] = {
	{ 136000000, 174000000, true },
	{ 400000000, 470000000, true },
};

// Two levels (3): the stock menu's "Low, High". The radio refuses TX until the calibration's bias tables are read
static const char *const power_names[] = { "L", "H" };

// docs/ux/information-architecture.md 7.7. F and EXT are the keys printed so (4); SK1 is the side key on KEYIN2, SK2
// the one on KEYIN3
static const keymap_entry_t keymap[] = {
	{ KEY_F, ACT_SELECT, ACT_SELECT, false, false, 0 },
	{ KEY_EXT, ACT_BACK, ACT_BACK, false, false, 0 },
	{ KEY_UP, ACT_NAV_UP, ACT_NAV_UP, false, true, 0 },
	{ KEY_DOWN, ACT_NAV_DOWN, ACT_NAV_DOWN, false, true, 0 },
#if CONFIG_DUAL_WATCH
	{ KEY_AB, ACT_AB, ACT_DUAL_WATCH, false, false, 0 },
#else
	{ KEY_AB, ACT_ZONE_UP, ACT_ZONE_UP, false, false, 0 },
#endif
	{ KEY_STAR, ACT_QUICK, ACT_QUICK, false, false, '*' },
	{ KEY_HASH, ACT_CONTEXT, ACT_CONTEXT, false, false, '#' },
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
	{ KEY_SK1, ACT_MONITOR, ACT_MONITOR, true, false, 0 },
	{ KEY_SK2, ACT_VFO_TOGGLE, ACT_COPY_VFO, false, false, 0 },
	{ KEY_PTT, ACT_PTT, ACT_PTT, true, false, 0 },
};

// Reassignable in Settings > Keys and the codeplug (core/input/keyassign.h): the side keys, then A/B
static const uint8_t side_keys[] = { KEY_SK1, KEY_SK2, KEY_AB };
static const char *const side_key_names[] = { "SK1", "SK2", "A/B" };


static const at32_host_uart_cfg_t host_uart = {
	.usart = USART6,
	.irq = USART6_IRQn,
	.baud = 115200,
};

const board_t board = {
	.name = "RT-4D",
	.caps = CAP_KEYPAD | (CONFIG_FREQ_DETECT ? CAP_FREQ_DETECT : 0),
	.bands = bands,
	.band_count = sizeof(bands) / sizeof(bands[0]),
	.power_count = sizeof(power_names) / sizeof(power_names[0]),
	.power_names = power_names,
	.power_default = 0,	// low
	.display = &rt4d_display,
	.input = &rt4d_input,
	.keymap = keymap,
	.keymap_len = sizeof(keymap) / sizeof(keymap[0]),
	.side_keys = side_keys,
	.side_key_names = side_key_names,
	.side_key_count = sizeof(side_keys),
	.radio = &rt4d_radio,
#if CONFIG_DMR_CALL
	.dmr_call = &rt4d_dmr_call,
#endif
	.power = &rt4d_power,
	.flash = &rt4d_flash,
#if CONFIG_HOSTLINK
	.host_port = &at32_host_uart_stream,
#endif
	.system = &at32_system,
	.chip_reg = rt4d_chip_reg,
#if CONFIG_DEBUGLINK
	.view_line = rt4d_view_line,
#endif
	// The stock warnings (4): 6.0 V low, 5.8 V where it stops
	.battery_low_mv = 6000,
	.battery_critical_mv = 5800,
};

// Outputs at their safe levels before anything slow: the HF6853 off (5.5), the amplifier off, the data flash and the
// display deselected, the backlight on. The LEDs only when SWD isn't kept (pins.h)
static void reset_levels(void)
{
	PIN_OUT(PIN_DIG_POWER, false);
	PIN_OUT(PIN_AMP_EN, false);
	PIN_OUT(PIN_BACKLIGHT, true);
	PIN_OUT(PIN_FLASH_CS, true);
	PIN_OUT(PIN_LCD_CS, true);
	PIN_OUT(PIN_BCAST_POWER, false);
#if !defined(RT4D_KEEP_SWD)
	PIN_OUT(PIN_LED_RED, false);
	PIN_OUT(PIN_LED_GREEN, false);
#endif
}

void board_early_init(void)
{
	reset_levels();
	(void)at32_clock_init();	// stays on the 8 MHz HICK if the PLL never locks: slow, but it runs
	// The PC link: USART6 on PA11 / PA12. RX is also the PTT to ground, so it gets a pull-up for a radio with no cable
	at32_pin_mux(PIN_PORT(PIN_HOST_TXD), PIN_MASK(PIN_HOST_TXD), HOST_UART_MUX, false, GPIO_PULL_NONE);
	at32_pin_mux(PIN_PORT(PIN_HOST_RXD), PIN_MASK(PIN_HOST_RXD), HOST_UART_MUX, false, GPIO_PULL_UP);
	at32_host_uart_configure(&host_uart);
}

// Everything RadioFoundry stores is from 7 MB up in the 16 MB data flash, far above what the stock application uses
// (hardware-rt4d.md 3.3, whose highest known area is the font at 0x14C000): the open-firmware codeplug's 64 KB "EEPROM"
// image at 0x700000, the settings ring and the sector journal after it, the 1 MB open-firmware "flash" image at
// 0x720000 with room above it for the DMR ID database, the native codeplug in the last 1 MB. The factory calibration
// (sector 0) is read only, so the open-firmware CPS gets no calibration window
static const hal_flash_map_t flash_map = {
	.settings_addr = 0x711000u,
	.settings_size = 0x008000u,	// 8 sectors
	.journal_addr = 0x719000u,	// 2 sectors
	.openfw_eeprom = 0x700000u,
	.openfw_flash = 0x720000u,
	.openfw_end = 0xF00000u,
	.native_addr = 0xF00000u,
	.native_size = 0x100000u,
	// The stock application's DMR key records (hardware-rt4d.md 5.4.1; dmr_rt4d.c): no raw CPS read returns them
	.secret_addr = 0x082000u,
	.secret_size = 0x001000u,
};

static const at32_spi_flash_cfg_t flash_cfg = {
	.spi = SPI2,
	.mdiv = 1,	// PCLK1 / 4: 18 MHz at 72 MHz
	.bus_port = PIN_PORT(PIN_FLASH_SCK),
	.bus_pins = PIN_MASK(PIN_FLASH_SCK) | PIN_MASK(PIN_FLASH_MISO) | PIN_MASK(PIN_FLASH_MOSI),
	.mux = SPI_FLASH_MUX,
	.cs_port = PIN_PORT(PIN_FLASH_CS),
	.cs_pin = PIN_MASK(PIN_FLASH_CS),
	.write_floor = 0x700000u,
	.map = &flash_map,
};

void board_init(void)
{
	AT32_PROBE(7);	// the first task runs
	rt4d_power.init();
	at32_spi_flash_configure(&flash_cfg);
	if (rt4d_flash.init()) {
		(void)rt4d_cal_load(&rt4d_flash);
	}
	AT32_PROBE(8);	// power, data flash and calibration done
}
