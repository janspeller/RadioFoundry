// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Exercise the real factory reader with synthetic primary/mirror records, never real calibration data.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include FACTORY_CAL_SOURCE

static uint8_t primary[0x20000], mirror[0x20000], metadata[0x20000], replacement[0x20000];
static uint8_t identity_primary[0x20000], identity_mirror[0x20000];
// The board's fingerprint check is stubbed; this test exercises selection, not the shared CRC implementation.
uint32_t storage_crc32(uint32_t crc, const void *buf, uint32_t n)
{
	assert(crc == 0 && n == 25);
	return ((const uint8_t *)buf)[0] == 0xA5 ? 0xD0D91952u : 0;
}
static bool fail_control_read, fail_analog_read, fail_deviation_read, raw_ecc_failed;
bool CAL_NAND_READ(uint32_t addr, void *out, uint32_t n)
{
	if (addr < sizeof(metadata) && n <= sizeof(metadata) - addr) {
		memcpy(out, metadata + addr, n); return true;
	}
	if (addr >= 0x03000000u && addr - 0x03000000u + n <= sizeof(replacement)) {
		memcpy(out, replacement + addr - 0x03000000u, n); return true;
	}
	if (fail_deviation_read && (addr == CAL_A + 6u || addr == CAL_B + 6u)) return false;
	if (fail_control_read && (addr == CAL_A || addr == CAL_B)) return false;
	if (fail_analog_read && (addr == CAL_A + 0x40u || addr == CAL_B + 0x40u)) return false;
	if (addr >= 0x02FA0000u && addr - 0x02FA0000u + n <= sizeof(identity_primary)) {
		memcpy(out, identity_primary + addr - 0x02FA0000u, n); return true;
	}
	if (addr >= 0x02FC0000u && addr - 0x02FC0000u + n <= sizeof(identity_mirror)) {
		memcpy(out, identity_mirror + addr - 0x02FC0000u, n); return true;
	}
	if (addr >= CAL_A && addr - CAL_A + n <= sizeof(primary)) {
		memcpy(out, primary + addr - CAL_A, n); return true;
	}
	if (addr >= CAL_B && addr - CAL_B + n <= sizeof(mirror)) {
		memcpy(out, mirror + addr - CAL_B, n); return true;
	}
	return false;
}
bool CAL_NAND_READ_FACTORY(uint32_t addr, void *out, uint32_t n)
{
	return CAL_NAND_READ(addr, out, n);
}
bool CAL_NAND_READ_ANY(uint32_t addr, void *out, uint32_t n, uint32_t *failed)
{
	*failed = raw_ecc_failed ? 1 : 0;
	return CAL_NAND_READ(addr, out, n);
}
static void record(uint8_t *bank, uint16_t value)
{
	memset(bank, 1, sizeof(primary));
	memcpy(bank + CAL_MARK_OFF, cal_mark, sizeof(cal_mark));
	bank[0] = (uint8_t)value; bank[1] = (uint8_t)(value >> 8);
}
static void expect(bool valid, uint16_t expected)
{
	uint16_t value = 0;
	assert(CAL_CONTROL(&value) == valid);
	assert(value == expected);
}
#if CONFIG_FM
static void test_rf_deviation(void)
{
	memset(metadata, 0xFF, sizeof(metadata)); metadata[0x1FF00] = 0xA5;
	metadata[0x1F4] = 0x55; metadata[0x1F5] = 0; metadata[0x1F6] = 0; metadata[0x1F7] = 0;
	record(primary, 3620); record(mirror, 1901);
	const uint8_t normal[] = { 0x2A, 0x12, 0x0C };
	memcpy(primary + 6, normal, sizeof(normal));
	mirror[6] = 0x55; mirror[7] = 0x13; mirror[8] = 0x0D;
	assert(CAL_LOAD());
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x0A92);
	assert(CAL_RF_DEVIATION(145500000u, false) == 0x0A92); // both bands use the first record
	assert(CAL_RF_DEVIATION(433500000u, true) == 0x0A8C);
	assert(CAL_RF_DEVIATION(477750000u, false) == 0x0E92); // exact class 1 overrides class-2 interval
	assert(CAL_RF_DEVIATION(455000000u, false) == 0x0C12); // exact class 2 fills a gap
	assert(CAL_RF_DEVIATION(151670000u, false) == 0x0C12); // VHF class 1 adds six, not sixteen
	assert(CAL_RF_DEVIATION(136500000u, false) == 0x0ED2); // VHF class 2 adds seventeen
	const struct { uint32_t hz; uint8_t expected; } classes[] = {
		{ 400499990u, 0 }, { 400500000u, 2 }, { 400700009u, 2 }, { 400700010u, 0 },
		{ 432300000u, 1 }, { 432302490u, 0 }, { 432302500u, 2 }, { 433300000u, 2 },
		{ 433300010u, 0 }, { 140293750u, 1 }, { 166831250u, 1 }, { 159250000u, 2 },
		{ 300000000u, 0 }, { 300000010u, 0 }, { 520000000u, 2 },
	};
	for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
		assert(rf_deviation_class(classes[i].hz) == classes[i].expected);
	primary[6] = 0xFF;
	assert(CAL_RF_DEVIATION(477750000u, false) == 0x03D2); // addition wraps at eight bits
	primary[6] = 0; primary[7] = 0xC0; primary[8] = 0x80;
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x00C0); // subtone byte is ORed unmasked
	assert(CAL_RF_DEVIATION(433500000u, true) == 0x0080);
	memset(primary + 6, 0, 3);
	assert(CAL_RF_DEVIATION(433500000u, false) == 0);
	memset(primary + 6, 0xFF, 3);
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x0A92); // defensive erased-record policy
	fail_deviation_read = true;
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x0A92);
	fail_deviation_read = false;
	memcpy(primary + CAL_RETIRE_OFF, retire_mark, sizeof(retire_mark));
	assert(CAL_LOAD());
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x1553);
	assert(CAL_RF_DEVIATION(433500000u, true) == 0x154D);
	// PA-table failure does not erase independent calibration for this stage.
	record(primary, 3620); primary[6] = 0x30; primary[7] = 5; primary[8] = 6;
	memset(primary + CAL_UHF_0, 0xFF, CAL_UHF_LEN);
	assert(!CAL_LOAD());
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x0C05);
	// The saved replacement map must select the calibrated replacement, not stale primary bytes.
	record(primary, 1000); record(replacement, 3620);
	replacement[6] = 0x55; replacement[7] = 0x13; replacement[8] = 0x0D;
	metadata[0] = (uint8_t)(CAL_A >> 17); metadata[1] = (uint8_t)(CAL_A >> 25);
	metadata[0xFA] = 0x80; metadata[0xFB] = 1; metadata[0x1F6] = 1;
	assert(CAL_LOAD());
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x1553);
	metadata[0x1FF00] = 0;
	assert(!CAL_LOAD());
	assert(CAL_RF_DEVIATION(433500000u, false) == 0x0A92); // reload clears stale base
}
#endif

static void test_rf_identity(void)
{
	memset(metadata, 0xFF, sizeof(metadata)); metadata[0x1FF00] = 0xA5;
	metadata[0x1F4] = 0x55; metadata[0x1F5] = 0; metadata[0x1F6] = 0; metadata[0x1F7] = 0;
	record(primary, 3620); record(mirror, 1901);
	record(identity_primary, 0); record(identity_mirror, 0);
	memcpy(identity_primary + 0x120, "1846N", 5);
	memcpy(identity_mirror + 0x120, "1846S", 5);
	assert(CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_N);
	memcpy(identity_primary + CAL_RETIRE_OFF, retire_mark, sizeof(retire_mark));
	assert(CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_OTHER);
	memcpy(identity_mirror + 0x120, "1846N", 5);
	assert(CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_N);
	memcpy(identity_mirror + CAL_RETIRE_OFF, retire_mark, sizeof(retire_mark));
	assert(CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_UNKNOWN);
	record(identity_primary, 0); memcpy(identity_primary + 0x120, "1846n", 5);
	assert(CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_OTHER); // exact, case-sensitive hardware ID
	memcpy(identity_primary + 0x120, "1846N", 5);
	memset(primary + CAL_UHF_0, 0xFF, CAL_UHF_LEN);
	assert(!CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_N); // identity is independent of PA data
	record(primary, 3620); record(identity_primary, 0); record(replacement, 0);
	memcpy(identity_primary + 0x120, "1846S", 5); memcpy(replacement + 0x120, "1846N", 5);
	metadata[0] = (uint8_t)(0x02FA0000u >> 17); metadata[1] = (uint8_t)(0x02FA0000u >> 25);
	metadata[0xFA] = 0x80; metadata[0xFB] = 1; metadata[0x1F6] = 1;
	assert(CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_N); // mapped identity, not stale raw bytes
	metadata[0x1FF00] = 0;
	assert(!CAL_LOAD() && CAL_RF_TYPE() == CAL_KIND_UNKNOWN);
}

int main(void)
{
	expect(false, 0);
	memset(metadata, 0xFF, sizeof(metadata));
	metadata[0x1FF00] = 0xA5;
	metadata[0x1F4] = 0x55; metadata[0x1F5] = 0;
	metadata[0x1F6] = 0; metadata[0x1F7] = 0;
	record(primary, 3620); record(mirror, 1901);
	const uint8_t misc_values[5] = { 0x12,0x34,0x56,0x78,9 };
	const uint8_t analog_values[10] = { 0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,0x01,0x23 };
	const uint8_t mirror_analog[10] = { 0x23,0x01,0xF0,0xDE,0xBC,0x9A,0x78,0x56,0x34,0x12 };
	const uint8_t analog_defaults[10] = { 0x46,0x50,0x05,0x78,0x09,0xC4,0x05,0xDC,0x01,0x0E };
	memcpy(primary + 0x4A, misc_values, sizeof(misc_values));
	memcpy(primary + 0x40, analog_values, sizeof(analog_values));
	memcpy(mirror + 0x40, mirror_analog, sizeof(mirror_analog));
	assert(CAL_LOAD()); expect(true, 3620);
	CAL_AUDIO_TYPE audio;
	CAL_AUDIO_READ(&audio);
	assert(memcmp(audio.misc, misc_values, sizeof(misc_values)) == 0);
	assert(memcmp(audio.analog, analog_values, sizeof(analog_values)) == 0);
	fail_analog_read = true;
	CAL_AUDIO_READ(&audio);
	assert(memcmp(audio.analog, analog_defaults, sizeof(analog_defaults)) == 0);
	fail_analog_read = false;
	assert(!CAL_CONTROL(NULL));
	primary[CAL_MARK_OFF] = 0;
	assert(CAL_LOAD()); expect(true, 1901);
	CAL_AUDIO_READ(&audio);
	assert(memcmp(audio.analog, mirror_analog, sizeof(mirror_analog)) == 0);
	record(primary, 3620);
	assert(CAL_LOAD());
	memset(primary + 0x40, 0xFF, 10);
	CAL_AUDIO_READ(&audio);
	assert(memcmp(audio.analog, analog_defaults, sizeof(analog_defaults)) == 0);
	memset(primary + 0x40, 0, 10);
	CAL_AUDIO_READ(&audio);
	for (unsigned i = 0; i < 10; i++) assert(audio.analog[i] == 0); // zero is an intentional gain, not erased data
	memcpy(primary + CAL_RETIRE_OFF, retire_mark, sizeof(retire_mark));
	assert(CAL_LOAD()); expect(true, 1901); // the old reader incorrectly selected this retired primary
	memcpy(mirror + CAL_RETIRE_OFF, retire_mark, sizeof(retire_mark));
	assert(!CAL_LOAD()); expect(false, 0); // never use a retired mirror as a fallback
	for (unsigned i = 0; i < 2; i++) {
		record(primary, i ? 4095 : 0);
		assert(CAL_LOAD()); expect(true, i ? 4095 : 0);
	}
	record(primary, 4096);
	assert(CAL_LOAD()); expect(false, 0); // do not truncate an invalid factory word
	record(primary, 0xFFFF);
	assert(CAL_LOAD()); expect(false, 0);
	record(primary, 3620);
	fail_control_read = true;
	assert(CAL_LOAD()); expect(false, 0);
	fail_control_read = false;
	assert(CAL_LOAD()); expect(true, 3620);
	// A failed power table must not discard a valid independent DC-control calibration.
	memset(primary + CAL_UHF_0, 0xFF, CAL_UHF_LEN);
	assert(!CAL_LOAD()); expect(true, 3620);
	memset(primary, 0xFF, sizeof(primary)); memset(mirror, 0xFF, sizeof(mirror));
	assert(!CAL_LOAD()); expect(false, 0); // reload must not retain a stale value
	// A relocated factory block must be read through the saved map, never through the stale raw address.
	record(primary, 1000); record(replacement, 3620);
	memcpy(replacement + 0x40, analog_values, sizeof(analog_values));
	metadata[0] = (uint8_t)(CAL_A >> 17); metadata[1] = (uint8_t)(CAL_A >> 25);
	metadata[0xFA] = 0x80; metadata[0xFB] = 1; metadata[0x1F6] = 1;
	assert(CAL_LOAD()); expect(true, 3620);
	assert(calibration_map.count == 1 && map_status == 4);
	CAL_AUDIO_READ(&audio);
	assert(memcmp(audio.analog, analog_values, sizeof(analog_values)) == 0);
	uint8_t window[32];
	assert(CAL_BYTES(3, 0, 0, window, 2) == 2 && window[0] == 0x24 && window[1] == 0x0E);
	assert(CAL_BYTES(3, 2, 0, window, 8) == 8);
	assert(memcmp(window + 4, cal_mark, 4) == 0);
	assert(CAL_BYTES(2, 0, 0, window, 2) == -1);
	assert(CAL_BYTES(3, 8, 0, window, 2) == -1);
	assert(CAL_BYTES(3, 4, 0, window, 3) == 3 && window[0] == 0 && window[1] == 0x24 && window[2] == 0x0E);
	raw_ecc_failed = true;
	assert(CAL_BYTES(3, 6, 0, window, 9) == 9 && window[0] == 1);
	raw_ecc_failed = false;
	assert(CAL_BYTES(3, 4, 0, window, 1) == -1);
	assert(CAL_BYTES(3, 0, 255, window, 2) == -1);
	assert(CAL_BYTES(3, 2, 7, window, 2) == -1);
	assert(CAL_BYTES(3, 0, 0, window, 0) == -1);
	assert(CAL_BYTES(3, 0, 0, window, 33) == -1);
	assert(CAL_BYTES(3, 0, 0, NULL, 2) == -1);
	metadata[0x1FF00] = 0;
	assert(!CAL_LOAD()); expect(false, 0);
	assert(!calibration_map.ready && map_status == 2);
	assert(CAL_BYTES(3, 0, 0, window, 2) == -1);
	metadata[0x1FF00] = 0xA5;
	metadata[0x1F6] = 90;
	assert(!CAL_LOAD()); expect(false, 0);
	assert(map_status == 3);
#if CONFIG_FM
	test_rf_deviation();
#endif
	test_rf_identity();
	puts("factoryDacTest PASSED");
	return 0;
}
