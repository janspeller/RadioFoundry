// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D factory calibration (docs/spec/hardware-rt4d.md 3.2): one 0x70-byte record per band at 0x20 + band x 0x70 in
// the data flash, read once and never written. Only the two transmit bands' records are kept in RAM (the receive-only
// bands aren't used by this port yet); the squelch tables at 0x30-0x60 aren't used either (the core sets its own
// squelch from RSSI and noise).
#include <stddef.h>
#include "cal_rt4d.h"

#define CAL_OFF_REF		0x00u
#define CAL_OFF_BIAS_HIGH	RT4D_CAL_OFF_BIAS_HIGH
#define CAL_OFF_BIAS_LOW	RT4D_CAL_OFF_BIAS_LOW
#define CAL_STEP_HZ		5000000u
#define CAL_UHF_FROM_HZ		240000000u
#define TX_BANDS		2u	// bands 0 (136-174) and 1 (400-480)

typedef struct {
	uint32_t lo_hz, hi_hz, base_hz;
} cal_range_t;

// Bands 0-10 of 3.2: range and index base. The ranges meet, so the first that holds hz wins
static const cal_range_t ranges[RT4D_CAL_BANDS] = {
	{ 136000000u, 174000000u, 135000000u },
	{ 400000000u, 480000000u, 400000000u },
	{ 18000000u, 64000000u, 15000000u },
	{ 64000000u, 136000000u, 60000000u },
	{ 174000000u, 240000000u, 170000000u },
	{ 240000000u, 320000000u, 240000000u },
	{ 320000000u, 400000000u, 320000000u },
	{ 480000000u, 560000000u, 480000000u },
	{ 560000000u, 660000000u, 560000000u },
	{ 840000000u, 920000000u, 840000000u },
	{ 920000000u, 1000000000u, 920000000u },
};

typedef struct {
	uint16_t ref_offset;			// 0x00: REG_38/39 = f + ref_offset - 0x8000 (10 Hz units)
	uint8_t bias[2][RT4D_CAL_ENTRIES];	// [power level: 0 low, 1 high] REG_36 bias, 6 bits
} cal_band_t;

static cal_band_t cal[TX_BANDS];
static bool valid;
// Every band's reference offset, for frequency detect (3.1.1): 0x8000 (none) where a record isn't plausible
static uint16_t ref_all[RT4D_CAL_BANDS];

int rt4d_cal_band(uint32_t hz, bool *uhf)
{
	if (uhf != NULL) {
		*uhf = hz >= CAL_UHF_FROM_HZ;
	}
	for (unsigned b = 0; b < RT4D_CAL_BANDS; b++) {
		if (hz >= ranges[b].lo_hz && hz <= ranges[b].hi_hz) {
			return (int)b;
		}
	}
	return -1;
}

static unsigned entry(unsigned band, uint32_t hz)
{
	uint32_t i = (hz - ranges[band].base_hz) / CAL_STEP_HZ;
	return i < RT4D_CAL_ENTRIES ? i : RT4D_CAL_ENTRIES - 1u;
}

// An erased or never-written record reads 0xFF: its offset 0xFFFF and every bias 0x3F after masking
static bool plausible(const uint8_t *rec)
{
	uint16_t ref = (uint16_t)(rec[CAL_OFF_REF] | rec[CAL_OFF_REF + 1u] << 8);
	if (ref == 0xFFFFu || ref == 0x0000u) {
		return false;
	}
	for (unsigned i = 0; i < RT4D_CAL_ENTRIES; i++) {
		if (rec[CAL_OFF_BIAS_HIGH + i] != 0xFFu || rec[CAL_OFF_BIAS_LOW + i] != 0xFFu) {
			return true;
		}
	}
	return false;
}

bool rt4d_cal_load(const hal_flash_t *flash)
{
	uint8_t rec[CAL_OFF_BIAS_LOW + RT4D_CAL_ENTRIES];
	valid = false;
	for (unsigned b = 0; b < RT4D_CAL_BANDS; b++) {
		uint8_t r[2];
		uint16_t ref = 0;
		if (flash->read(RT4D_CAL_BASE + b * RT4D_CAL_RECORD + CAL_OFF_REF, r, sizeof r)) {
			ref = (uint16_t)(r[0] | r[1] << 8);
		}
		ref_all[b] = ref == 0xFFFFu || ref == 0x0000u ? 0x8000u : ref;
	}
	for (unsigned b = 0; b < TX_BANDS; b++) {
		if (!flash->read(RT4D_CAL_BASE + b * RT4D_CAL_RECORD, rec, sizeof(rec)) || !plausible(rec)) {
			return false;
		}
		cal[b].ref_offset = (uint16_t)(rec[CAL_OFF_REF] | rec[CAL_OFF_REF + 1u] << 8);
		for (unsigned i = 0; i < RT4D_CAL_ENTRIES; i++) {
			cal[b].bias[1][i] = rec[CAL_OFF_BIAS_HIGH + i] & 0x3Fu;
			cal[b].bias[0][i] = rec[CAL_OFF_BIAS_LOW + i] & 0x3Fu;
		}
	}
	valid = true;
	return true;
}

bool rt4d_cal_valid(void)
{
	return valid;
}

uint32_t rt4d_cal_detect_hz(uint32_t raw_10hz, uint32_t range_hz)
{
	int b = rt4d_cal_band(range_hz, NULL);
	uint32_t ref = b >= 0 ? ref_all[b] : 0x8000u;
	if (ref == 0u) {
		ref = 0x8000u;		// before rt4d_cal_load: no offset
	}
	uint32_t f = raw_10hz + RT4D_DETECT_ADD;
	return f > ref ? (f - ref) * 10u : 0u;
}

uint32_t rt4d_cal_freq_10hz(uint32_t hz)
{
	int b = rt4d_cal_band(hz, NULL);
	uint32_t f = hz / 10u;
	if (!valid || b < 0 || (unsigned)b >= TX_BANDS) {
		return f;
	}
	return f + cal[b].ref_offset - 0x8000u;
}

uint8_t rt4d_cal_bias(uint32_t hz, uint8_t level)
{
	int b = rt4d_cal_band(hz, NULL);
	if (!valid || b < 0 || (unsigned)b >= TX_BANDS || level > 1u) {
		return 0;
	}
	return cal[b].bias[level][entry((unsigned)b, hz)];
}
