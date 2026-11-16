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
static uint32_t cal_base;

// The factory's regions are read with the chip's on-die ECC off: a checked read refuses them, which is what a
// region written without that ECC looks like (nand_d878uv2.c). The diagnostic inspection further down keeps the
// checked read on purpose, so an ECC failure can still be seen rather than hidden
static bool raw_read(uint32_t addr, void *out, uint32_t len, void *ctx)
{
	(void)ctx;
	return d878uv2_nand_read_factory(addr, out, len);
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

static void tune_build(void);	// defined below, called as the block is loaded

bool d878uv2_cal_load(void)
{
	loaded = false;
	control = 0;
	control_valid = false;
	calibration_map.ready = false;
	uint8_t signature[25];
	map_status = 1;
	signature_crc = 0;
	if (!d878uv2_nand_read_factory(0x1FF00u, signature, sizeof(signature))) return false;
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
	cal_base = base;
	uint8_t word[2];
	if (cal_read(base, word, sizeof(word))) {
		uint16_t raw = (uint16_t)(word[0] | ((uint16_t)word[1] << 8));
		if (raw <= 4095u) {
			control = raw;
			control_valid = true;
		}
	}
	tune_build();
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

// The modulator's gain, which is this radio's deviation: the 16-bit big-endian word at +0x10 of the band record,
// sent to the baseband unchanged, with no arithmetic (docs/spec/hardware-d878uv2.md 4.6, 5.3). The UHF record
// serves above 300 MHz and the VHF one below. 0x0DAC is the factory default, used when the calibration has not
// been read
#define CAL_BAND_UHF		0x0000u
#define CAL_BAND_VHF		0x0020u
#define CAL_BAND_MOD_GAIN	0x0080u		// +0x10 of the audio record at +0x0080 / +0x00C0
#define CAL_AUDIO_UHF		0x0080u
#define CAL_AUDIO_VHF		0x00C0u
#define CAL_MOD_GAIN_OFF	0x10u
#define CAL_MOD_GAIN_DEFAULT	0x0DACu

uint16_t d878uv2_cal_mod_gain(uint32_t hz)
{
	uint8_t w[2];
	uint32_t rec = hz > 300000000u ? CAL_AUDIO_UHF : CAL_AUDIO_VHF;

	if (!loaded || !cal_read(cal_base + rec + CAL_MOD_GAIN_OFF, w, sizeof(w))) {
		return CAL_MOD_GAIN_DEFAULT;
	}
	uint16_t v = (uint16_t)((w[0] << 8) | w[1]);	// big-endian in the record, sent as it stands

	return v != 0u && v != 0xFFFFu ? v : CAL_MOD_GAIN_DEFAULT;
}

// The audio record's per-radio values (docs/spec/hardware-d878uv2.md 4.6.6, 5.3). Three gain bytes that the
// baseband passes to the codec, and three 16-bit levels that are baseband fields of their own. The factory
// defaults stand in when the calibration has not been read; they are the values a factory-fresh radio runs
#define CAL_A_LOL		0x12u	// -> the codec's LOL driver gain      (default 0x04)
#define CAL_A_LOR		0x13u	// -> LOR                              (default 0x08)
#define CAL_A_MICR		0x14u	// -> the right MICPGA                 (default 0x0C)
#define CAL_A_DEMOD		0x17u	// -> DEMOD_GAIN,     u16 big-endian   (default 0x0000)
#define CAL_A_SPK		0x19u	// -> DIGI_SPK_GAIN,  u16 big-endian   (default 0x0546)
#define CAL_A_MIC		0x1Bu	// -> DIGI_MIC_GAIN,  u16 big-endian   (default 0x07D0)

void d878uv2_cal_audio(d878uv2_cal_audio_t *a)
{
	uint8_t r[0x1Du];

	a->lol = 0x04;
	a->lor = 0x08;
	a->mic_r = 0x0C;
	a->demod = 0x0000;
	a->spk_gain = 0x0546;
	a->mic_gain = 0x07D0;
	a->from_radio = false;
	if (!loaded || !cal_read(cal_base + CAL_AUDIO_UHF, r, sizeof(r))) {
		return;
	}
	// A byte of 0xFF is an erased cell, not a gain: keep the factory value for anything that reads blank
	if (r[CAL_A_LOL] != 0xFFu) {
		a->lol = r[CAL_A_LOL];
	}
	if (r[CAL_A_LOR] != 0xFFu) {
		a->lor = r[CAL_A_LOR];
	}
	if (r[CAL_A_MICR] != 0xFFu) {
		a->mic_r = r[CAL_A_MICR];
	}
	uint16_t demod = (uint16_t)((r[CAL_A_DEMOD] << 8) | r[CAL_A_DEMOD + 1u]);
	uint16_t spk = (uint16_t)((r[CAL_A_SPK] << 8) | r[CAL_A_SPK + 1u]);
	uint16_t mic = (uint16_t)((r[CAL_A_MIC] << 8) | r[CAL_A_MIC + 1u]);

	if (demod != 0xFFFFu) {
		a->demod = demod;
	}
	if (spk != 0xFFFFu && spk != 0u) {
		a->spk_gain = spk;
	}
	if (mic != 0xFFFFu && mic != 0u) {
		a->mic_gain = mic;
	}
	a->from_radio = true;
}

// The PA6 tuning voltage (docs/spec/hardware-d878uv2.md 5.1.2). Each band record carries three 12-bit PWM
// duties - at the bottom, middle and top of its band - and the firmware expands them into a straight line
// between the points, then interpolates inside it at every frequency change. The duty is NOT one constant
// across a band, and a port that never drives PA6 leaves the VCO or front-end untuned.
#define CAL_TUNE_LOW		0x0Au	// u16 little-endian in the band record
#define CAL_TUNE_MID		0x0Cu
#define CAL_TUNE_HIGH		0x0Eu
#define TUNE_UHF_POINTS		13u	// 400..520 MHz on a 10 MHz grid, the last point 0x1000
#define TUNE_VHF_POINTS		39u	// 136..174 MHz on a 1 MHz grid
#define TUNE_UHF_BASE_HZ	400000000u
#define TUNE_VHF_BASE_HZ	136000000u

static uint16_t tune_uhf[TUNE_UHF_POINTS];
static uint16_t tune_vhf[TUNE_VHF_POINTS];
static bool tune_ready;

static uint16_t band_word(uint32_t band_rec, uint8_t off)
{
	uint8_t w[2];

	if (!cal_read(cal_base + band_rec + off, w, sizeof(w))) {
		return 0;
	}
	return (uint16_t)(w[0] | ((uint16_t)w[1] << 8));	// little-endian in the record
}

// A straight line from a to b across n steps, written into t[0..n]
static void tune_span(uint16_t *t, uint16_t a, uint16_t b, unsigned n)
{
	for (unsigned i = 0; i <= n; i++) {
		t[i] = (uint16_t)((int32_t)a + ((int32_t)b - (int32_t)a) * (int32_t)i / (int32_t)n);
	}
}

static void tune_build(void)
{
	uint16_t lo = band_word(CAL_BAND_UHF, CAL_TUNE_LOW);
	uint16_t mid = band_word(CAL_BAND_UHF, CAL_TUNE_MID);
	uint16_t hi = band_word(CAL_BAND_UHF, CAL_TUNE_HIGH);

	tune_ready = false;
	if (lo == 0u || mid == 0u || hi == 0u || lo > 4095u || mid > 4095u || hi > 4095u) {
		return;
	}
	tune_span(tune_uhf, lo, mid, 4);		// point 0 low, point 4 mid
	tune_span(&tune_uhf[4], mid, hi, 4);		// point 8 high
	tune_span(&tune_uhf[8], hi, 0x1000u > 4095u ? 4095u : 0x1000u, 4);	// point 12

	lo = band_word(CAL_BAND_VHF, CAL_TUNE_LOW);
	mid = band_word(CAL_BAND_VHF, CAL_TUNE_MID);
	hi = band_word(CAL_BAND_VHF, CAL_TUNE_HIGH);
	if (lo == 0u || mid == 0u || hi == 0u || lo > 4095u || mid > 4095u || hi > 4095u) {
		return;
	}
	tune_span(tune_vhf, lo, mid, 19);		// point 0 low, point 0x13 mid
	tune_span(&tune_vhf[19], mid, hi, 19);		// point 0x26 high
	tune_ready = true;
}

// The duty for a frequency, interpolated inside its band's table in tenths of a step
bool d878uv2_cal_tune_duty(uint32_t hz, uint16_t *duty)
{
	if (!tune_ready) {
		return false;
	}
	const uint16_t *t;
	uint32_t idx, frac, last;

	if (hz >= TUNE_UHF_BASE_HZ) {
		uint32_t mhz = (hz - TUNE_UHF_BASE_HZ) / 1000000u;

		t = tune_uhf;
		last = TUNE_UHF_POINTS - 1u;
		idx = mhz / 10u;
		frac = mhz % 10u;
	} else if (hz >= TUNE_VHF_BASE_HZ) {
		t = tune_vhf;
		last = TUNE_VHF_POINTS - 1u;
		idx = (hz - TUNE_VHF_BASE_HZ) / 1000000u;
		frac = ((hz - TUNE_VHF_BASE_HZ) / 100000u) % 10u;
	} else {
		return false;
	}
	if (idx >= last) {
		*duty = t[last];
		return true;
	}
	int32_t a = t[idx], b = t[idx + 1u];

	*duty = (uint16_t)(a + (b - a) * (int32_t)frac / 10);
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

#if CONFIG_DIAG
// Restricted read-only windows: two calibration headers and their active/retired marker areas.
int d878uv2_cal_bytes(uint8_t chip, uint8_t page, uint8_t reg, uint8_t *out, uint8_t cap)
{
	if (chip != 3u || page > 7u || out == NULL || cap == 0 || cap > 32u) return -1;
	bool raw = page >= 4u;
	uint8_t n = raw ? (uint8_t)(cap - 1u) : cap;
	uint8_t window = page & 3u;
	uint32_t base = (window & 1u) ? CAL_B : CAL_A;
	unsigned limit = window < 2u ? 256u : 8u;
	if (n == 0 || (unsigned)reg + n > limit) return -1;
	if (window >= 2u) base += CAL_RETIRE_OFF;
	if (!raw) return cal_read(base + reg, out, n) ? n : -1;
	uint32_t physical, failed = 0;
	if (!nand_remap_address(&calibration_map, base + reg, &physical) ||
	    !d878uv2_nand_read_any(physical, out + 1, n, &failed)) return -1;
	out[0] = failed != 0; // Raw bytes are diagnostic only; preserve their ECC failure status.
	return cap;
}
#endif
