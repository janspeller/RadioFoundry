// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS display: an ST7735S-class 128x160 TFT on an 8-bit 8080 bus driven by GPIO
// (docs/spec/hardware-d878uv2.md 4.2): data on PE7-PE14, D/C, reset, chip select and the write strobe on PB10-PB13.
//
// Two things to watch:
//   - this board has no read strobe at all, so the data bus is never turned around for the panel;
//   - **the orientation.** The controller's own frame is 128 wide by 160 tall and the stock firmware draws it that
//     way with MADCTL 0x88 (hardware-d878uv2.md 4.2), but the glass sits rotated in the housing: the screen is
//     wider than it is high. So RadioFoundry uses the panel in landscape, 160 by 128, which needs the row/column
//     exchange the stock value does not set. The value below is the landscape correction the family's other
//     160x128 board needed; which way round this one lands, and whether red and blue come out swapped, is for the
//     bench (docs/testing/d878uv2-bringup.md, stage 2).
// The init sequence itself is recorded in hardware-d878uv2.md 4.2 and matches the AT-D868UV's value for value.
#include "d878uv2.h"
#include "config.h"
#include "pins.h"
#include "mcu_io.h"
#include "hal_time.h"
#include "display/st7735/st7735.h"

#define LCD_W		160u
#define LCD_H		128u

static bool ready;

// One byte: the data lines in one store (set and clear at once), then WR low and high. The panel takes the byte on
// WR's rising edge; WR low is held for two stores, above the 15 ns minimum at 120 MHz, and the whole cycle stays
// over the 66 ns write cycle. Only the data bits move, so PE0-PE6 and PE15 (the daughter board's status line) are
// left alone, as the stock firmware's masked store does
static inline void put_byte(uint8_t v)
{
	mcu_port_put(LCD_DATA_PORT, (uint16_t)(v << LCD_DATA_SHIFT), (uint16_t)((uint8_t)~v << LCD_DATA_SHIFT));
	mcu_pin_write(PIN_LCD_WR, false);
	mcu_pin_write(PIN_LCD_WR, false);
	mcu_pin_write(PIN_LCD_WR, true);
	mcu_pin_write(PIN_LCD_WR, true);
}

static void bus_begin(void)
{
	mcu_pin_write(PIN_LCD_CS, false);
}

static void bus_end(void)
{
	mcu_pin_write(PIN_LCD_CS, true);
}

static void bus_cmd(uint8_t c)
{
	mcu_pin_write(PIN_LCD_RS, false);	// low = command
	put_byte(c);
	mcu_pin_write(PIN_LCD_RS, true);
}

static void bus_data(const uint8_t *d, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		put_byte(d[i]);
	}
}

static void bus_pixels(const uint16_t *px, uint32_t n, bool swap_rb)
{
	for (uint32_t i = 0; i < n; i++) {
		uint16_t v = px[i];
		if (swap_rb) {
			v = (uint16_t)((v & 0x07E0) | (v >> 11) | (v << 11));
		}
		put_byte((uint8_t)(v >> 8));
		put_byte((uint8_t)v);
	}
}

static void bus_reset(bool level)
{
	mcu_pin_write(PIN_LCD_RESET, level);
}

static const st7735_bus_t bus = {
	.begin = bus_begin,
	.end = bus_end,
	.cmd = bus_cmd,
	.data = bus_data,
	.pixels = bus_pixels,
	.reset = bus_reset,
	.delay_ms = hal_delay_ms,
};

// Landscape: the panel is mounted rotated, so the frame is used the long way round. Type 0's base MADCTL is 0xA0
// and 0xA0 ^ 0xC8 = 0x68 (column/row exchange, mirrored, BGR); the stock firmware's own 0x88 is the unrotated
// frame and does not apply here. Orientation and the color order are to be checked on the radio
static const st7735_t panel = {
	.bus = &bus,
	.type = 0,
	.madctl_xor = 0xC8,
	.width = LCD_W,
	.height = LCD_H,
};

static void init(void)
{
	mcu_pin_out(PIN_LCD_CS, true, false);
	mcu_pin_out(PIN_LCD_RS, true, false);
	mcu_pin_out(PIN_LCD_WR, true, false);
	mcu_pin_out(PIN_LCD_RESET, true, false);
	// No read strobe on this board (hardware-d878uv2.md 4.2): PE15 is the daughter board's status line
	mcu_pin_out(LCD_DATA_PORT, LCD_DATA_MASK, false, false);
	st7735_init(&panel);
	ready = true;
}

static void flush(const void *fb, const uint16_t *palette, uint16_t y0, uint16_t y1)
{
	if (ready) {
		st7735_write_rows_indexed(&panel, (const uint8_t *)fb, palette, y0, y1);
	}
}

static void set_backlight(uint8_t percent)
{
	d878uv2_backlight(percent);
}

const hal_display_t d878uv2_display = {
	.width = LCD_W,
	.height = LCD_H,
	.bpp = 4,
	.inset_top = CONFIG_DISPLAY_INSET_TOP,
	.inset_bottom = CONFIG_DISPLAY_INSET_BOTTOM,
	.inset_left = CONFIG_DISPLAY_INSET_LEFT,
	.inset_right = CONFIG_DISPLAY_INSET_RIGHT,
	.init = init,
	.flush_indexed = flush,
	.set_backlight = set_backlight,
};
