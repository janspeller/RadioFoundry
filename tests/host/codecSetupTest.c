// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Send the board's real audio profile through voice setup; a microphone needs routing as well as gain.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include VOICE_SETUP_HEADER
#include "modem/sct/sct_analog.h"

void AUDIO_CAL_READ(AUDIO_CAL_TYPE *a)
{
	*a = (AUDIO_CAL_TYPE){ .lol=4, .lor=8, .mic_r=12, .spk_gain=1350, .mic_gain=2000,
		.misc={ 0x80,0,0x80,0,0 }, .analog={ 0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,0x01,0x23 } };
}
static uint8_t response[8];
static uint16_t left;
static uint32_t now;
static uint8_t expected_mic_gain = 0x20;
static bool mic_routed, mic_unmuted, mic_digital_unity, mic_agc, mic_ceiling, reject_route;
static bool analog_test, modulation_sent;
static const uint8_t expected_analog[] = { 0x80,0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,0x01,0x23 };
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
			if (p[7] == 0 && p[8] == 86) {
				assert(p[9] == 0xA0); // changing gain must not change AGC target or hysteresis
				mic_agc = true;
			}
			if (p[7] == 0 && p[8] == 88) {
				assert(p[9] == expected_mic_gain);
				mic_ceiling = true;
			}
		} else {
			id = p[6];
			if (analog_test && id == SCT_F_MOD_GAIN) {
				assert(n == 10 && p[7] == 0x0D && p[8] == 0xAC);
				modulation_sent = true;
			}
			if (analog_test && id == SCT_F_WORK_MODE) assert(modulation_sent);
			if (analog_test && id == SCT_F_ANALOG_CONFIG) {
				assert(p[4] == 12 && memcmp(p + 7, expected_analog, sizeof(expected_analog)) == 0);
			}
			assert(id != SCT_F_CODEC_SELECT); // keep the resident codec selection while applying the board gains
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
static void reset(void *ctx) { (void)ctx; left = 0; mic_routed = mic_unmuted = mic_digital_unity = mic_agc = mic_ceiling = false; }
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
	assert(mic_routed && mic_unmuted && mic_digital_unity && mic_agc && mic_ceiling);
	assert(codec_mic_pga(0) == 32 && codec_mic_pga(10) == 52 && codec_mic_pga(-10) == 12);
	assert(codec_mic_pga(127) == 95 && codec_mic_pga(-128) == 0);
	codec_mic_offset(10); expected_mic_gain = 52;
	assert(sct_host_boot(&h, &rx, 1000) == SCT_BOOT_OK);
	assert(mic_routed && mic_unmuted && mic_agc && mic_ceiling);
	codec_mic_offset(-10); expected_mic_gain = 12;
	assert(sct_host_boot(&h, &rx, 1000) == SCT_BOOT_OK);
	assert(mic_routed && mic_unmuted && mic_agc && mic_ceiling);
	reject_route = true;
	assert(sct_host_boot(&h, &rx, 1000) == SCT_BOOT_NO_VOICE);
	// Exercise the real mode setup: a ready receiver must already have sent its modulation calibration.
	analog_test = true;
	sct_analog_t a;
	sct_analog_init(&a, write_bus, NULL);
	a.setup_fields = analog_setup;
	a.setup_count = sizeof(analog_setup) / sizeof(analog_setup[0]);
	a.config = analog_config;
	sct_analog_request(&a, true, false, true);
	for (unsigned i = 0; i < 32 && !a.ready; i++) {
		sct_analog_poll(&a, now++);
		uint8_t buf[8];
		uint16_t n = read_bus(buf, sizeof(buf), NULL);
		if (n) sct_analog_packet(&a, buf, n);
	}
	assert(a.ready && a.error == 0 && modulation_sent);
	// The shared runtime codec owner must complete both coupled gain writes before analog TX readiness.
	reject_route = false;
	mic_routed = mic_unmuted = mic_agc = mic_ceiling = false;
	codec_mic_offset(10); expected_mic_gain = 52;
	sct_codec_t codec;
	sct_codec_init(&codec,write_bus,NULL);
	a.codec = &codec; a.tx_codec = codec_writes; a.tx_codec_count = CODEC_MIC_COUNT;
	sct_analog_request(&a,true,true,true);
	sct_analog_poll(&a,now++);
	assert(!a.ready);
	for (unsigned i=0; i<32 && !a.ready; i++) {
		sct_analog_poll(&a,now);
		sct_codec_poll(&codec,now++);
		uint8_t buf[8];
		uint16_t n=read_bus(buf,sizeof(buf),NULL);
		if (n && !sct_codec_packet(&codec,buf,n)) sct_analog_packet(&a,buf,n);
	}
	assert(a.ready && a.error == 0 && codec.acked == 4 && codec.error == 0);
	assert(mic_routed && mic_unmuted && mic_agc && mic_ceiling);
	puts("codecSetupTest PASSED");
	return 0;
}
