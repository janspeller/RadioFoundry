// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS factory calibration, read from the radio's own NAND (docs/spec/hardware-d878uv2.md 5).
//
// The block is kept as an A/B pair 128 KB apart. An active copy has its new-copy marker and no retirement
// marker in the last eight bytes. The primary is tried first; retired or unmarked copies are not used.
//
// **Read only.** These values are unique to this radio and no reflash of the stock firmware brings them back, so
// nothing here ever writes to the block. The port's NAND write window starts far above it anyway
// (nand_d878uv2.c).
//
// What this file uses so far are the four transmit power tables per band, one per power level: 39 entries on a
// 1 MHz grid from 136 MHz, and 121 entries on a 1 MHz grid from 400 MHz. Each entry is the 8-bit value the
// transmit power DAC takes at that megahertz (4.3). The first calibration word also sets PA5's DC control.
// Logical reads follow the saved NAND replacement map; map reconstruction is never attempted.
#include "d878uv2.h"
#include "config.h"
#include <stddef.h>
#include "storage/storage.h"
#include "flash/nand_remap/nand_remap.h"

// The pair, and the tag that says a copy is complete
#define CAL_A		0x02F60000u
#define CAL_B		0x02F80000u
#define CAL_MARK_OFF	0x1FFFCu
#define CAL_RETIRE_OFF	0x1FFF8u
static const uint8_t cal_mark[4] = { 0x55, 0x55, 0xAA, 0xAA };
static const uint8_t retire_mark[4] = { 0xAA, 0xAA, 0x55, 0x55 };

// The transmit power tables: four levels, VHF then UHF (5)
#define CAL_UHF_0	0x0200u		// 121 entries each, 0x80 apart
#define CAL_UHF_STRIDE	0x0080u
#define CAL_UHF_LEN	121u
#define CAL_UHF_BASE_HZ	400000000u
#define CAL_VHF_0	0x0400u		// 39 entries each, 0x40 apart
#define CAL_VHF_STRIDE	0x0040u
#define CAL_VHF_LEN	39u
#define CAL_VHF_BASE_HZ	136000000u
#define CAL_LEVELS	4u
#define MHZ		1000000u

static uint8_t uhf[CAL_LEVELS][CAL_UHF_LEN];
static uint8_t vhf[CAL_LEVELS][CAL_VHF_LEN];
static bool loaded;
// No nominal DAC value is safe to apply when the saved calibration cannot be read.
static uint16_t control;
static bool control_valid;
static nand_remap_t calibration_map;
static uint8_t map_status;
static uint32_t signature_crc;

static bool raw_read(uint32_t addr, void *out, uint32_t len, void *ctx)
{
	(void)ctx;
	return d878uv2_nand_read(addr, out, len);
}

static bool cal_read(uint32_t addr, void *out, uint32_t len)
{
	return nand_remap_read(&calibration_map, addr, out, len);
}

static bool copy_good(uint32_t base)
{
	uint8_t mark[8];

	if (!cal_read(base + CAL_RETIRE_OFF, mark, sizeof(mark))) {
		return false;
	}
	bool retired = true;
	for (unsigned i = 0; i < sizeof(cal_mark); i++) {
		if (mark[i + 4] != cal_mark[i]) return false;
		if (mark[i] != retire_mark[i]) retired = false;
	}
	return !retired;
}

// A table is believable when it is neither all erased nor all zero: a blank or unwritten block must not drive the
// transmitter. Anything else is the factory's and is taken as it stands
static bool table_plausible(const uint8_t *t, unsigned n)
{
	unsigned ff = 0, zero = 0;

	for (unsigned i = 0; i < n; i++) {
		ff += t[i] == 0xFF;
		zero += t[i] == 0;
	}
	return ff != n && zero != n;
}

bool d878uv2_cal_load(void)
{
	loaded = false;
	control = 0;
	control_valid = false;
	calibration_map.ready = false;
	uint8_t signature[25];
	map_status = 1;
	signature_crc = 0;
	if (!d878uv2_nand_read(0x1FF00u, signature, sizeof(signature))) return false;
	signature_crc = storage_crc32(0, signature, sizeof(signature));
	map_status = 2;
	if (signature_crc != 0xD0D91952u) return false;
	map_status = 3;
	if (!nand_remap_load(&calibration_map, raw_read, NULL, 2048, 0x180, 0x200)) return false;
	map_status = 4;
	uint32_t base = copy_good(CAL_A) ? CAL_A : copy_good(CAL_B) ? CAL_B : 0;
	if (base == 0) {
		return false;
	}
	uint8_t word[2];
	if (cal_read(base, word, sizeof(word))) {
		uint16_t raw = (uint16_t)(word[0] | ((uint16_t)word[1] << 8));
		if (raw <= 4095u) {
			control = raw;
			control_valid = true;
		}
	}
	for (unsigned l = 0; l < CAL_LEVELS; l++) {
		if (!cal_read(base + CAL_UHF_0 + l * CAL_UHF_STRIDE, uhf[l], CAL_UHF_LEN) ||
		    !cal_read(base + CAL_VHF_0 + l * CAL_VHF_STRIDE, vhf[l], CAL_VHF_LEN)) {
			return false;
		}
		if (!table_plausible(uhf[l], CAL_UHF_LEN) || !table_plausible(vhf[l], CAL_VHF_LEN)) {
			return false;
		}
	}
	loaded = true;
	return true;
}

bool d878uv2_cal_ready(void)
{
	return loaded;
}

// The transmit power DAC value for a level and a frequency, from the band's table on its 1 MHz grid. Out of band
// the nearest entry stands, as the stock firmware's own clamp does. False when no calibration was read: the caller
// must not transmit on a guess
bool d878uv2_cal_tx_drive(uint8_t level, uint32_t hz, uint8_t *drive)
{
	if (!loaded || level >= CAL_LEVELS) {
		return false;
	}
	const uint8_t *t;
	uint32_t idx;

	if (hz >= CAL_UHF_BASE_HZ) {
		idx = (hz - CAL_UHF_BASE_HZ) / MHZ;
		if (idx >= CAL_UHF_LEN) {
			idx = CAL_UHF_LEN - 1u;
		}
		t = uhf[level];
	} else {
		idx = hz > CAL_VHF_BASE_HZ ? (hz - CAL_VHF_BASE_HZ) / MHZ : 0;
		if (idx >= CAL_VHF_LEN) {
			idx = CAL_VHF_LEN - 1u;
		}
		t = vhf[level];
	}
	*drive = t[idx];
	return true;
}

// PA5's calibrated DC control is shared across bands, separate from PA power and PA6 tracking PWM.
// False means unavailable: callers must not enable or write the control DAC.
bool d878uv2_cal_control(uint16_t *value)
{
	if (value == NULL) return false;
	*value = control;
	return control_valid;
}

int d878uv2_cal_map_count(void)
{
	return calibration_map.ready ? calibration_map.count : -1;
}

uint32_t d878uv2_cal_map_info(uint8_t key)
{
	return key == 0 ? map_status : signature_crc;
}
