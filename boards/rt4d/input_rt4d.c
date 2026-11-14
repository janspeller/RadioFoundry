// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D keypad, A/B and side keys, PTT (docs/spec/hardware-rt4d.md 4). KEYOUT1-4 are driven one at a time and KEYIN1-4
// read (the keymatrix driver's rows and columns). A/B and the two side keys pull KEYIN1-3 to ground with nothing
// driven (keymatrix.h's direct keys). The PTT is the cable's RX line, so it goes through the shared-line gate.
#include "rt4d.h"
#include "pins.h"
#include "platform.h"
#include "hal_time.h"
#include "input/keymatrix/keymatrix.h"
#include "input/sharedptt/shared_ptt.h"

#define OUTS		4u
#define INS		4u
#define N		KEYMATRIX_NONE
// Each KEYIN has 1 nF to ground (4): a pressed key pulls it down through the driven line, which takes far less than
// this; with nothing pressed it stays at the pull-up
#define SETTLE_US	10u

// [KEYOUT][KEYIN]: the matrix layout of 4, KEYOUTn a column of the switch sheet, KEYIN1-4 its keys top to bottom
static const uint8_t map[OUTS * INS] = {
	KEY_F, KEY_1, KEY_4, KEY_7,
	KEY_UP, KEY_2, KEY_5, KEY_8,
	KEY_DOWN, KEY_3, KEY_6, KEY_9,
	KEY_EXT, KEY_STAR, KEY_0, KEY_HASH,
};
static const uint8_t direct[INS] = { KEY_AB, KEY_SK1, KEY_SK2, N };
static const keymatrix_t matrix = { .rows = OUTS, .cols = INS, .map = map, .direct = direct };

typedef struct {
	GPIO_Type *port;
	uint16_t pin;
} pin_t;

static const pin_t outs[OUTS] = {
	{ PIN_PORT(PIN_KEYOUT1), PIN_MASK(PIN_KEYOUT1) },
	{ PIN_PORT(PIN_KEYOUT2), PIN_MASK(PIN_KEYOUT2) },
	{ PIN_PORT(PIN_KEYOUT3), PIN_MASK(PIN_KEYOUT3) },
	{ PIN_PORT(PIN_KEYOUT4), PIN_MASK(PIN_KEYOUT4) },
};
static const pin_t ins[INS] = {
	{ PIN_PORT(PIN_KEYIN1), PIN_MASK(PIN_KEYIN1) },
	{ PIN_PORT(PIN_KEYIN2), PIN_MASK(PIN_KEYIN2) },
	{ PIN_PORT(PIN_KEYIN3), PIN_MASK(PIN_KEYIN3) },
	{ PIN_PORT(PIN_KEYIN4), PIN_MASK(PIN_KEYIN4) },
};

static uint32_t last_raw, stable;
static shared_ptt_t ptt_gate;

static uint8_t read_ins_low(void)
{
	uint8_t low = 0;
	for (uint8_t i = 0; i < INS; i++) {
		if (!at32_pin_read(ins[i].port, ins[i].pin)) {
			low |= (uint8_t)(1u << i);
		}
	}
	return low;
}

static uint32_t scan_matrix(void)
{
	uint8_t row_low[OUTS];
	hal_delay_us(SETTLE_US);
	uint8_t idle_low = read_ins_low();
	for (uint8_t r = 0; r < OUTS; r++) {
		at32_pin_write(outs[r].port, outs[r].pin, false);
		hal_delay_us(SETTLE_US);
		row_low[r] = read_ins_low();
		at32_pin_write(outs[r].port, outs[r].pin, true);
	}
	return keymatrix_decode(&matrix, idle_low, row_low);
}

static void init(void)
{
	for (uint8_t i = 0; i < OUTS; i++) {
		at32_pin_out(outs[i].port, outs[i].pin, true, false);
	}
	for (uint8_t i = 0; i < INS; i++) {
		at32_pin_in(ins[i].port, ins[i].pin, GPIO_PULL_UP);
	}
}

// Called every 10 ms: a change is reported once two scans in a row agree
static uint32_t scan(void)
{
	uint32_t raw = scan_matrix();
	shared_ptt_line_t line = { .flagged = at32_host_uart_line_errors(), .keyed = rt4d_radio_transmitting() };
	line.rx_seen = at32_host_uart_last_rx(&line.rx_ms);
	if (shared_ptt_pressed(&ptt_gate, !PIN_READ(PIN_HOST_RXD), &line, hal_millis())) {
		raw |= 1u << KEY_PTT;
	}
	if (raw == last_raw) {
		stable = raw;
	}
	last_raw = raw;
	return stable;
}

static int8_t encoder(void)
{
	return 0;
}

const hal_input_t rt4d_input = {
	.init = init,
	.scan = scan,
	.encoder = encoder,
};

uint32_t rt4d_input_ptt_ignored(void)
{
	return ptt_gate.ignored;
}
