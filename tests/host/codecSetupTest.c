// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Send the board's real audio profile through voice setup; a microphone needs routing as well as gain.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include VOICE_SETUP_HEADER

void AUDIO_CAL_READ(AUDIO_CAL_TYPE *a)
{
	*a = (AUDIO_CAL_TYPE){ .lol=4, .lor=8, .mic_r=12, .spk_gain=1350, .mic_gain=2000,
		.misc={ 0x80,0,0x80,0,0 } };
}
static uint8_t response[8];
static uint16_t left;
static uint32_t now;
static uint8_t expected_mic_gain = D878UV2_MIC_GAIN;
static bool mic_routed, mic_unmuted, mic_digital_unity, reject_route;
static bool write_bus(const uint8_t *p, uint16_t n, void *ctx)
{
	(void)ctx;
	uint8_t id = 0x17, status = 1, type = 0;
	if (n != SCT_BOOT_BLOCK_BYTES) {
		type = p[5]; status = 0;
		if (type == 0x40) {
			assert(n == 12 && p[4] == 6);
			if (p[7] == 1 && p[8] == 52) {
				assert(p[9] == 0x80); // IN1L must be connected to the left MICPGA
				mic_routed = true;
				if (reject_route) status = 7;
			}
			if (p[7] == 1 && p[8] == 59) {
				assert(p[9] == expected_mic_gain);
				mic_unmuted = !(p[9] & 0x80);
			}
		} else {
			id = p[6];
			if (id == 0x68) {
				assert(n == 10 && p[7] == 0x07 && p[8] == 0xD0);
				mic_digital_unity = true;
			}
		}
	}
	uint8_t ack[] = { 0x84,0xA9,0x61,0,2,type,id,status };
	memcpy(response, ack, sizeof(ack)); left = sizeof(ack);
	return true;
}
static uint16_t read_bus(uint8_t *p, uint16_t cap, void *ctx)
{
	(void)ctx;
	uint16_t n = left < cap ? left : cap;
	memcpy(p, response + sizeof(response) - left, n); left -= n;
	return n;
}
static void delay(uint32_t ms) { now += ms; }
static uint32_t clock_ms(void) { return now; }
static void reset(void *ctx) { (void)ctx; left = 0; mic_routed = mic_unmuted = mic_digital_unity = false; }
int main(void)
{
	static uint8_t block[SCT_BOOT_BLOCK_BYTES];
	codec_audio_from_cal();
	const uint8_t sq[] = {0,0,0,60,15,60,10,140,255,255,255,255};
	const uint8_t misc[] = {8,0,8,0,128,0,128,0,0,0,0,0};
	assert(memcmp(sq_analog,sq,12)==0 && memcmp(misc_gain,misc,12)==0);
	assert(sq_digital[8]==15 && sq_digital[9]==60 && sq_digital[10]==10 && sq_digital[11]==140);
	assert(CODEC_MIC_COUNT == 4);
	assert(codec_writes[0].page == 1 && codec_writes[0].reg == 52 && codec_writes[0].value == 0x80);
	assert(codec_writes[1].page == 1 && codec_writes[1].reg == 59 && !(codec_writes[1].value & 0x80));
	sct_host_t h = { .write = write_bus, .read = read_bus, .reset = reset, .delay_ms = delay,
		.now_ms = clock_ms, .boot = SCT_BOOT_BLOCK, .boot_block = block, .voice = &voice };
	sct_rx_t rx;
	assert(sct_host_boot(&h, &rx, 1000) == SCT_BOOT_OK);
	assert(mic_routed && mic_unmuted && mic_digital_unity);
	assert(codec_mic_pga(0) == 32 && codec_mic_pga(10) == 52 && codec_mic_pga(-10) == 12);
	assert(codec_mic_pga(127) == 95 && codec_mic_pga(-128) == 0);
	codec_mic_offset(10); expected_mic_gain = 52;
	assert(sct_host_boot(&h, &rx, 1000) == SCT_BOOT_OK);
	assert(mic_routed && mic_unmuted);
	reject_route = true;
	assert(sct_host_boot(&h, &rx, 1000) == SCT_BOOT_NO_VOICE);
	puts("codecSetupTest PASSED");
	return 0;
}
