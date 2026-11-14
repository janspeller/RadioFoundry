// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS keys and channel knob (docs/spec/hardware-d878uv2.md 4.1).
//
// The key matrix is driven on the LCD's data lines PE7-PE14, one line low at a time, and read on six pull-up
// inputs; keys are active low. The stock firmware scans between LCD writes, and so does this: the scan leaves the
// data bus high, and the display's own writes set it again on every byte.
//
// **The map is empty.** Which printed key sits at which matrix position is not recorded anywhere: the image gave
// the pins and the scanning scheme, not the layout. Until a bench session fills it in, no matrix key reports, and
// d878uv2_key_raw() reports the raw sense reading per drive line so that session can map them in one sitting
// (docs/testing/d878uv2-bringup.md, stage 3). Nothing here guesses a layout: a plausible-looking wrong map would
// be harder to find than no map at all.
#include "d878uv2.h"
#include "config.h"
#include "pins.h"
#include "mcu_io.h"
#include "input/keymatrix/keymatrix.h"
#include "input/quadrature/quadrature.h"

#define ROWS	8u	// the LCD data lines PE7-PE14
#define COLS	6u	// the sense inputs

// The sense lines, in column order. Bit n of a scan byte is low when column n reads low
static mcu_gpio_t *const sense_port[COLS] = { GPIOA, GPIOA, GPIOB, GPIOB, GPIOC, GPIOE };
static const uint16_t sense_bit[COLS] = {
	MCU_PIN(10), MCU_PIN(14), MCU_PIN(1), MCU_PIN(2), MCU_PIN(5), MCU_PIN(1),
};

// Not recorded: every position is KEYMATRIX_NONE until the bench fills it in (hardware-d878uv2.md 4.1). The order
// is row 0 (PE7) first, six columns to a row
static const uint8_t key_map[ROWS * COLS] = {
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
};

// No column-to-ground key is known on this board either
static const uint8_t key_direct[COLS] = {
	KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE, KEYMATRIX_NONE,
};

static const keymatrix_t matrix = {
	.rows = ROWS,
	.cols = COLS,
	.map = key_map,
	.direct = key_direct,
};

static quad_dec_t knob;
static uint8_t last_raw[ROWS];

// The settle between driving a line and reading the senses. The stock firmware uses a short busy loop; at 120 MHz
// this is a few hundred nanoseconds, well over the RC of a pull-up on a short trace
static inline void settle(void)
{
	for (volatile int i = 0; i < 16; i++) {
	}
}

static uint8_t read_sense(void)
{
	uint8_t low = 0;
	for (unsigned c = 0; c < COLS; c++) {
		if (!mcu_pin_read(sense_port[c], sense_bit[c])) {
			low |= (uint8_t)(1u << c);
		}
	}
	return low;
}

// All drive lines high: the state the LCD leaves them in between bytes, and the state a scan returns them to
static inline void drive_all_high(void)
{
	mcu_port_put(KEY_DRIVE_PORT, (uint16_t)(0xFFu << KEY_DRIVE_SHIFT), 0);
}

static inline void drive_one_low(unsigned row)
{
	uint16_t bit = (uint16_t)(1u << (KEY_DRIVE_SHIFT + row));
	mcu_port_put(KEY_DRIVE_PORT, (uint16_t)((0xFFu << KEY_DRIVE_SHIFT) & ~bit), bit);
}

static uint8_t knob_ab(void)
{
	uint8_t ab = 0;
	if (mcu_pin_read(PIN_KNOB_A)) {
		ab |= 1u;
	}
	if (mcu_pin_read(PIN_KNOB_B)) {
		ab |= 2u;
	}
	return ab;
}

static void init(void)
{
	for (unsigned c = 0; c < COLS; c++) {
		mcu_pin_in(sense_port[c], sense_bit[c], MCU_PULL_UP);
	}
	mcu_pin_in(PIN_KNOB_A, MCU_PULL_UP);
	mcu_pin_in(PIN_KNOB_B, MCU_PULL_UP);
	// The drive lines are the LCD's data bus and the display's init already made them outputs; leave them high
	drive_all_high();
	quad_init(&knob, 4, knob_ab(), knob_ab());
}

static uint32_t scan(void)
{
	uint8_t row_low[ROWS];

	drive_all_high();
	settle();
	uint8_t idle_low = read_sense();

	for (unsigned r = 0; r < ROWS; r++) {
		drive_one_low(r);
		settle();
		row_low[r] = read_sense();
		last_raw[r] = row_low[r];
	}
	drive_all_high();

	// The knob is sampled with the scan: a detent is slower than the UI's scan interval
	(void)quad_sample(&knob, knob_ab());

	return keymatrix_decode(&matrix, idle_low, row_low);
}

static int8_t encoder(void)
{
	int32_t taken = 0;
	return quad_take(&knob, &taken);
}

static void encoder_stats(hal_encoder_stats_t *st)
{
	st->detents = knob.detents;
	st->transitions = knob.transitions;
	st->illegal = knob.illegal;
	st->reversals = knob.reversals;
	st->steps_per_detent = knob.steps;
}

void d878uv2_key_raw(uint8_t raw[ROWS])
{
	for (unsigned r = 0; r < ROWS; r++) {
		raw[r] = last_raw[r];
	}
}

const hal_input_t d878uv2_input = {
	.init = init,
	.scan = scan,
	.encoder = encoder,
	.encoder_stats = encoder_stats,
};
