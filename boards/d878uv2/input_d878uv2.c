// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS keys, PTT and channel knob (docs/spec/hardware-d878uv2.md 4.1.1, 4.3).
//
// The matrix is driven on the LCD's data lines PE7-PE14, one line low at a time, and read on three pull-up
// inputs; keys are active low. The stock firmware scans between LCD writes, and so does this: the scan leaves the
// data bus high, and the display's own writes set it again on every byte.
//
// Four keys are not matrix positions: they are wired to ground and read with every drive line high. PTT is PA10,
// PF1 is PE1 and PF2 is PA14. **PF3 shares PB2 with matrix column 1**, so while PF3 is held that column cannot be
// scanned - which is exactly what drivers/input/keymatrix's `direct` array is for: it reports PF3 and ignores
// column 1's two keys for as long as it is down.
//
// PA13 is not a key at all. It reads through an internal pull-down and the stock firmware uses it to gate the key
// task and pick an audio scaling, which is the shape of an accessory detect; the board leaves it alone.
#include "d878uv2.h"
#include "config.h"
#include "pins.h"
#include "mcu_io.h"
#include "input/keymatrix/keymatrix.h"
#include "input/quadrature/quadrature.h"

#define ROWS	8u	// the LCD data lines PE7-PE14
#define COLS	3u	// the matrix sense inputs

// The sense lines, in column order. Bit n of a scan byte is low when column n reads low
static mcu_gpio_t *const sense_port[COLS] = { GPIOB, GPIOB, GPIOC };
static const uint16_t sense_bit[COLS] = { MCU_PIN(1), MCU_PIN(2), MCU_PIN(5) };

// The map (hardware-d878uv2.md 4.1.1). Row 0 is PE7 and row 7 is PE14; the columns are PB1, PB2, PC5 in that
// order. Column 1 carries only P1 and P2, on the first two rows; the other six of its positions are unused
static const uint8_t key_map[ROWS * COLS] = {
	KEY_MENU,	KEY_P1,		KEY_1,
	KEY_UP,		KEY_P2,		KEY_2,
	KEY_DOWN,	KEYMATRIX_NONE,	KEY_3,
	KEY_7,		KEYMATRIX_NONE,	KEY_4,
	KEY_8,		KEYMATRIX_NONE,	KEY_5,
	KEY_9,		KEYMATRIX_NONE,	KEY_6,
	KEY_HASH,	KEYMATRIX_NONE,	KEY_0,
	KEY_EXIT,	KEYMATRIX_NONE,	KEY_STAR,
};

// PF3 is wired from column 1 straight to ground: the decoder reports it and drops column 1's matrix keys while it
// is held, which is what the hardware forces anyway
static const uint8_t key_direct[COLS] = { KEYMATRIX_NONE, KEY_PF3, KEYMATRIX_NONE };

static const keymatrix_t matrix = {
	.rows = ROWS,
	.cols = COLS,
	.map = key_map,
	.direct = key_direct,
};

static quad_dec_t knob;
static uint8_t last_raw[ROWS];
static uint8_t last_direct;
static bool ptt_prev;

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

// The direct inputs as "pressed" bits, for the diagnostics line. PF3 is not here: the matrix decoder takes it
// from the idle column reading, where the hardware puts it
#define DIRECT_PTT	0x01u
#define DIRECT_PF1	0x04u
#define DIRECT_PF2	0x08u
#define DIRECT_ACC	0x10u	// PA13, the accessory detect: not a key

static uint8_t read_direct(void)
{
	uint8_t v = 0;

	if (!mcu_pin_read(PIN_PTT)) {
		v |= DIRECT_PTT;
	}
	if (!mcu_pin_read(PIN_PF1)) {
		v |= DIRECT_PF1;
	}
	if (!mcu_pin_read(PIN_PF2)) {
		v |= DIRECT_PF2;
	}
	if (mcu_pin_read(PIN_ACCESSORY_DETECT)) {
		v |= DIRECT_ACC;
	}
	return v;
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
	mcu_pin_in(PIN_PTT, MCU_PULL_UP);
	mcu_pin_in(PIN_PF1, MCU_PULL_UP);
	mcu_pin_in(PIN_PF2, MCU_PULL_UP);
	mcu_pin_in(PIN_ACCESSORY_DETECT, MCU_PULL_DOWN);
	mcu_pin_in(PIN_KNOB_A, MCU_PULL_UP);
	mcu_pin_in(PIN_KNOB_B, MCU_PULL_UP);
	// The drive lines are the LCD's data bus and the display's init already made them outputs; leave them high
	drive_all_high();
	quad_init(&knob, 4, knob_ab(), knob_ab());
}

static uint32_t scan(void)
{
	uint8_t row_low[ROWS];

	// The direct inputs are read with every drive line high, as the stock firmware does
	drive_all_high();
	settle();
	uint8_t direct = read_direct();
	uint8_t idle_low = read_sense();

	for (unsigned r = 0; r < ROWS; r++) {
		drive_one_low(r);
		settle();
		row_low[r] = read_sense();
		last_raw[r] = row_low[r];
	}
	drive_all_high();
	last_direct = direct;

	// The knob is sampled with the scan: a detent is slower than the UI's scan interval
	(void)quad_sample(&knob, knob_ab());

	uint32_t keys = keymatrix_decode(&matrix, idle_low, row_low);
	// PF1 and PF2 are their own pins. PTT keys the transmitter, so it is reported only once two scans running
	// agree, the way the stock firmware confirms a press with a second read
	if (direct & DIRECT_PF1) {
		keys |= 1u << KEY_PF1;
	}
	if (direct & DIRECT_PF2) {
		keys |= 1u << KEY_PF2;
	}
	bool ptt = (direct & DIRECT_PTT) != 0;

	if (ptt && ptt_prev) {
		keys |= 1u << KEY_PTT;
	}
	ptt_prev = ptt;
	return keys;
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

void d878uv2_key_raw(uint8_t raw[8])
{
	for (unsigned r = 0; r < ROWS; r++) {
		raw[r] = last_raw[r];
	}
}

uint8_t d878uv2_key_direct(void)
{
	return last_direct;
}

const hal_input_t d878uv2_input = {
	.init = init,
	.scan = scan,
	.encoder = encoder,
	.encoder_stats = encoder_stats,
};
