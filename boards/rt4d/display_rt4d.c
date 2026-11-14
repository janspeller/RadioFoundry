// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D display: 128x64 monochrome ST7565 / ST7567-class LCD on a write-only serial bus, bit-banged (chip select PA15,
// clock PB3, command/data PB4, data PB5, reset PF6; docs/spec/hardware-rt4d.md 4). The backlight on PA3 is switched,
// not dimmed (no timer set up for it yet).
#include "rt4d.h"
#include "config.h"
#include "pins.h"
#include "platform.h"
#include "hal_time.h"
#include "display/st7567/st7567.h"

static bool ready;

// One byte, MSB first: data set while the clock is low, taken by the controller on the rising edge. The clock and
// the data are on port B: set / reset register writes only, so no other pin of the port is touched. The no-ops keep
// each clock phase above the controller's 50 ns serial clock minimum (ST7567 datasheet) at 72 MHz
static void send_byte(uint8_t v)
{
	const uint32_t clk = PIN_MASK(PIN_LCD_SCK), sda = PIN_MASK(PIN_LCD_SDA);
	for (uint8_t m = 0x80; m != 0; m >>= 1) {
		GPIOB->SCR = clk << 16 | ((v & m) ? sda : sda << 16);
		__NOP();
		__NOP();
		__NOP();
		__NOP();
		GPIOB->SCR = clk;
		__NOP();
		__NOP();
		__NOP();
		__NOP();
	}
}

static void bus_begin(void)
{
	PIN_WRITE(PIN_LCD_CS, false);
}

static void bus_end(void)
{
	PIN_WRITE(PIN_LCD_CS, true);
}

static void bus_cmd(uint8_t c)
{
	PIN_WRITE(PIN_LCD_CD, false);
	send_byte(c);
}

static void bus_data(const uint8_t *d, uint32_t n)
{
	PIN_WRITE(PIN_LCD_CD, true);
	for (uint32_t i = 0; i < n; i++) {
		send_byte(d[i]);
	}
}

static void bus_reset(bool level)
{
	PIN_WRITE(PIN_LCD_RST, level);
}

static const st7567_bus_t bus = {
	.begin = bus_begin,
	.end = bus_end,
	.cmd = bus_cmd,
	.data = bus_data,
	.reset = bus_reset,
	.delay_ms = hal_delay_ms,
};

// From the stock initialization (4): regulator ratio 5 (0x25), bias 1/9 (0xA2), SEG normal (0xA0). COM normal (0xC0),
// not the stock 0xC8: with 0xC8 our frame shows upside down on the radio (4, Orientation).
// Contrast: the stock byte before its settings load, 0x12 (4). The stock setting runs 0-30 and sends setting + 5, so a
// contrast setting, when the UI has one, stays in RT4D_CONTRAST_MIN..MAX. The column offset follows from SEG normal
// (column 0 first); unchecked on the radio
#define RT4D_CONTRAST_DEFAULT	0x12u
#define RT4D_CONTRAST_MIN	0x05u
#define RT4D_CONTRAST_MAX	0x23u
_Static_assert(RT4D_CONTRAST_DEFAULT >= RT4D_CONTRAST_MIN && RT4D_CONTRAST_DEFAULT <= RT4D_CONTRAST_MAX,
	       "the default contrast is outside the stock range");
static const st7567_t panel = {
	.bus = &bus,
	.col_offset = 0,
	.seg_reverse = false,
	.com_reverse = false,
	.regulator = 5,
	.contrast = RT4D_CONTRAST_DEFAULT,
	.bias = 0,
};

static void init(void)
{
	PIN_OUT(PIN_LCD_CS, true);
	PIN_OUT(PIN_LCD_SCK, true);
	PIN_OUT(PIN_LCD_CD, false);
	PIN_OUT(PIN_LCD_SDA, false);
	PIN_OUT(PIN_LCD_RST, true);
	st7567_init(&panel);
	ready = true;
}

static void flush(const void *fb, uint16_t y0, uint16_t y1)
{
	if (!ready || fb == NULL) {
		return;
	}
	st7567_write_rows(&panel, (const uint8_t *)fb, y0, y1);
}

static void set_backlight(uint8_t percent)
{
	PIN_WRITE(PIN_BACKLIGHT, percent != 0u);
}

const hal_display_t rt4d_display = {
	.inset_top = CONFIG_DISPLAY_INSET_TOP, .inset_bottom = CONFIG_DISPLAY_INSET_BOTTOM,
	.inset_left = CONFIG_DISPLAY_INSET_LEFT, .inset_right = CONFIG_DISPLAY_INSET_RIGHT,
	.width = ST7567_WIDTH,
	.height = ST7567_HEIGHT,
	.bpp = 1,
	.init = init,
	.flush = flush,
	.set_backlight = set_backlight,
};
