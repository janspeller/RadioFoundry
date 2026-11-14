// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS board internals shared between its files (docs/spec/hardware-d878uv2.md)
#ifndef D878UV2_H
#define D878UV2_H

#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "config.h"

extern const hal_display_t d878uv2_display;
extern const hal_input_t d878uv2_input;
extern const hal_power_t d878uv2_power;

// The keys by their printed names. The front panel is a 4 x 3 numeric keypad with MENU, EXIT and the two arrows
// above it; PF1 and PF2 are the side keys below PTT and PF3 is the orange key on top.
//
// **Which matrix position each key sits at is not recorded**, so the map in input_d878uv2.c is empty and no matrix
// key reports yet. d878uv2_key_raw() gives the raw sense reading per drive line so a bench session can map the
// printed keys onto positions and fill both that map and docs/spec/hardware-d878uv2.md 4.1
// (docs/testing/d878uv2-bringup.md, stage 3).
enum {
	KEY_MENU, KEY_EXIT, KEY_UP, KEY_DOWN,
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
	KEY_STAR, KEY_HASH,
	KEY_PF1, KEY_PF2, KEY_PF3,
	KEY_PTT,
	KEY_COUNT
};
_Static_assert(KEY_COUNT <= 32, "the key scan is a 32-bit mask");

// The key matrix scan, for the diagnostics line: the raw sense reading per drive line, so a bench session can map
// the printed keys onto positions without a build (hardware-d878uv2.md 4.1)
void d878uv2_key_raw(uint8_t raw[8]);

// LCD backlight, 0..100 %: TIM8 channel 2's complementary output on PB0 (power_d878uv2.c, hardware-d878uv2.md 4.3)
void d878uv2_backlight(uint8_t percent);

#endif
