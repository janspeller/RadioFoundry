// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D factory calibration (boards/rt4d/cal_rt4d.c) on a synthetic data flash: the band table, the 5 MHz index and its
// cap, the reference offset, the two bias tables, and the refusal of an erased record (docs/spec/hardware-rt4d.md 3.2)
#include <stdio.h>
#include <string.h>
#include "cal_rt4d.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint8_t mem[0x1000];
static bool flash_ok;

static bool rd(uint32_t addr, void *buf, uint32_t len)
{
	if (!flash_ok || addr + len > sizeof(mem)) {
		return false;
	}
	memcpy(buf, mem + addr, len);
	return true;
}

static const hal_flash_t flash = { .read = rd, .size = sizeof(mem), .erase_size = 0x1000 };

// Band b: reference offset ref, high-power bias 10 + entry + 20 x b, low-power bias 1 + entry (bit 7 set, masked away)
static void build(uint16_t ref0, uint16_t ref1)
{
	memset(mem, 0xFF, sizeof(mem));
	for (unsigned b = 0; b < 2; b++) {
		uint8_t *r = mem + RT4D_CAL_BASE + b * RT4D_CAL_RECORD;
		uint16_t ref = b ? ref1 : ref0;
		r[0] = (uint8_t)ref;
		r[1] = (uint8_t)(ref >> 8);
		for (unsigned i = 0; i < RT4D_CAL_ENTRIES; i++) {
			r[RT4D_CAL_OFF_BIAS_HIGH + i] = (uint8_t)(10 + i + 20 * b);
			r[RT4D_CAL_OFF_BIAS_LOW + i] = (uint8_t)(0x80 | (1 + i));
		}
	}
}

int main(void)
{
	bool uhf;
	CHECK(rt4d_cal_band(145000000, &uhf) == 0 && !uhf);
	CHECK(rt4d_cal_band(435000000, &uhf) == 1 && uhf);
	CHECK(rt4d_cal_band(200000000, &uhf) == 4 && !uhf);
	CHECK(rt4d_cal_band(250000000, &uhf) == 5 && uhf);	// UHF path from 240 MHz up
	CHECK(rt4d_cal_band(700000000, &uhf) == -1);
	CHECK(rt4d_cal_band(10000000, NULL) == -1);

	// No calibration yet: the plain frequency and no power
	CHECK(!rt4d_cal_valid());
	CHECK(rt4d_cal_freq_10hz(145000000) == 14500000u);
	CHECK(rt4d_cal_bias(145000000, 1) == 0);

	flash_ok = true;
	build(0x8000 + 120, 0x8000 - 35);
	CHECK(rt4d_cal_load(&flash) && rt4d_cal_valid());
	CHECK(rt4d_cal_freq_10hz(145000000) == 14500000u + 120u);
	CHECK(rt4d_cal_freq_10hz(435000000) == 43500000u - 35u);
	CHECK(rt4d_cal_freq_10hz(300000000) == 30000000u);	// a receive-only band: no record kept
	// Index (f - base) / 5 MHz: 136 MHz is entry 0 of band 0 (base 135), 145 MHz entry 2, capped at 15
	CHECK(rt4d_cal_bias(136000000, 1) == 10);
	CHECK(rt4d_cal_bias(145000000, 1) == 12);
	CHECK(rt4d_cal_bias(174000000, 1) == 17);
	CHECK(rt4d_cal_bias(400000000, 1) == 30);
	CHECK(rt4d_cal_bias(470000000, 1) == 44);
	CHECK(rt4d_cal_bias(480000000, 1) == 45);		// entry 16, capped at 15
	CHECK(rt4d_cal_bias(145000000, 0) == 3);		// the low table, 6 bits
	CHECK(rt4d_cal_bias(145000000, 2) == 0);		// no such level
	CHECK(rt4d_cal_bias(300000000, 1) == 0);

	// Frequency detect (3.1.1): raw + 0x8028 - the band's reference offset, 10 Hz units. A band whose record is erased
	// (band 5 here) has no offset (0x8000)
	CHECK(rt4d_cal_detect_hz(14500000u - 0x28u + 120u, 155000000) == 145000000u);
	CHECK(rt4d_cal_detect_hz(43500000u - 0x28u - 35u, 440000000) == 435000000u);
	CHECK(rt4d_cal_detect_hz(28000000u - 0x28u, 280000000) == 280000000u);
	mem[RT4D_CAL_BASE + 6 * RT4D_CAL_RECORD] = 0x10;	// band 6 (320-400): an offset of its own, 0x8010
	mem[RT4D_CAL_BASE + 6 * RT4D_CAL_RECORD + 1] = 0x80;
	CHECK(rt4d_cal_load(&flash));
	CHECK(rt4d_cal_detect_hz(36000000u - 0x28u + 0x10u, 360000000) == 360000000u);
	CHECK(rt4d_cal_detect_hz(0, 155000000) == 0);	// below 0 (band 0, offset 0x8078): nothing

	// An erased record, an unanswering flash: no calibration, no transmit
	build(0x8000, 0x8000);
	memset(mem + RT4D_CAL_BASE + RT4D_CAL_RECORD, 0xFF, RT4D_CAL_RECORD);
	CHECK(!rt4d_cal_load(&flash) && !rt4d_cal_valid());
	CHECK(rt4d_cal_bias(145000000, 1) == 0);
	build(0x8000, 0x8000);
	flash_ok = false;
	CHECK(!rt4d_cal_load(&flash) && !rt4d_cal_valid());

	printf("rt4dCalTest: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
	return failures != 0;
}
