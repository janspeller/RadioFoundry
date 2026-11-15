// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Exercise the real factory reader with synthetic primary/mirror records, never real calibration data.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include FACTORY_CAL_SOURCE

static uint8_t primary[0x20000], mirror[0x20000], metadata[0x20000], replacement[0x20000];
// The board's fingerprint check is stubbed; this test exercises selection, not the shared CRC implementation.
uint32_t storage_crc32(uint32_t crc, const void *buf, uint32_t n)
{
	assert(crc == 0 && n == 25);
	return ((const uint8_t *)buf)[0] == 0xA5 ? 0xD0D91952u : 0;
}
static bool fail_control_read;
bool CAL_NAND_READ(uint32_t addr, void *out, uint32_t n)
{
	if (addr < sizeof(metadata) && n <= sizeof(metadata) - addr) {
		memcpy(out, metadata + addr, n); return true;
	}
	if (addr >= 0x03000000u && addr - 0x03000000u + n <= sizeof(replacement)) {
		memcpy(out, replacement + addr - 0x03000000u, n); return true;
	}
	if (fail_control_read && (addr == CAL_A || addr == CAL_B)) return false;
	if (addr >= CAL_A && addr - CAL_A + n <= sizeof(primary)) {
		memcpy(out, primary + addr - CAL_A, n); return true;
	}
	if (addr >= CAL_B && addr - CAL_B + n <= sizeof(mirror)) {
		memcpy(out, mirror + addr - CAL_B, n); return true;
	}
	return false;
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
int main(void)
{
	expect(false, 0);
	memset(metadata, 0xFF, sizeof(metadata));
	metadata[0x1FF00] = 0xA5;
	metadata[0x1F4] = 0x55; metadata[0x1F5] = 0;
	metadata[0x1F6] = 0; metadata[0x1F7] = 0;
	record(primary, 3620); record(mirror, 1901);
	assert(CAL_LOAD()); expect(true, 3620);
	assert(!CAL_CONTROL(NULL));
	primary[CAL_MARK_OFF] = 0;
	assert(CAL_LOAD()); expect(true, 1901);
	record(primary, 3620);
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
	metadata[0] = (uint8_t)(CAL_A >> 17); metadata[1] = (uint8_t)(CAL_A >> 25);
	metadata[0xFA] = 0x80; metadata[0xFB] = 1; metadata[0x1F6] = 1;
	assert(CAL_LOAD()); expect(true, 3620);
	assert(calibration_map.count == 1 && map_status == 4);
	metadata[0x1FF00] = 0;
	assert(!CAL_LOAD()); expect(false, 0);
	assert(!calibration_map.ready && map_status == 2);
	metadata[0x1FF00] = 0xA5;
	metadata[0x1F6] = 90;
	assert(!CAL_LOAD()); expect(false, 0);
	assert(map_status == 3);
	puts("factoryDacTest PASSED");
	return 0;
}
