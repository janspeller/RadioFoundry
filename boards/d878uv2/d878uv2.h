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
// The four direct inputs as pressed bits: 0 PTT (PA10), 1 PA14, 2 PE1, 3 PA13. Which key the last three are was
// not identified (hardware-d878uv2.md 4.3)
uint8_t d878uv2_key_direct(void);

// --- SPI NAND (nand_d878uv2.c, docs/spec/hardware-d878uv2.md 4.1) ---
// RadioFoundry's own blocks, above everything the stock firmware writes (it stops at block 1741). The same layout
// the family's other SCT3258 board uses on the same 2 Gbit part
#define D878UV2_NAND_IDDB_FIRST		1792u
#define D878UV2_NAND_IDDB_BLOCKS	224u
#define D878UV2_NAND_IDDB_LOGICAL	192u	// logical blocks: 24 MB
#define D878UV2_NAND_RFOUNDRY_FIRST	2016u
#define D878UV2_NAND_RFOUNDRY_BLOCKS	32u

bool d878uv2_nand_init(void);
bool d878uv2_nand_ready(void);
uint16_t d878uv2_nand_id(void);
bool d878uv2_nand_read(uint32_t addr, void *buf, uint32_t len);
bool d878uv2_nand_read_any(uint32_t addr, void *buf, uint32_t len, uint32_t *ecc_failed);
void d878uv2_nand_lock(void);
void d878uv2_nand_unlock(void);
#if CONFIG_NAND_STORAGE
#include "flash/nand_sectors/nand_sectors.h"
extern const nand_dev_t d878uv2_nand_dev;
extern const nand_dev_t d878uv2_nand_iddb_dev;
#endif

// LCD backlight, 0..100 %: TIM8 channel 2's complementary output on PB0 (power_d878uv2.c, hardware-d878uv2.md 4.3)
void d878uv2_backlight(uint8_t percent);

#endif
