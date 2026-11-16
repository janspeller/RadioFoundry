// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Run the real RF initialization, scheduling GPS power immediately when its task is created.
#include <assert.h>
#include <stdio.h>
#include RF_STARTUP_SOURCE

unsigned host_dac_init_mask, host_dac_writes[2];
uint16_t host_dac_values[2];
static uint16_t saved_control = 3620;
static bool control_valid;
bool RF_CAL_CONTROL(uint16_t *value) { *value = saved_control; return control_valid; }
static int8_t mic_offset;
void RF_MODEM_MIC_GAIN(int8_t db) { mic_offset = db; }
static unsigned cal_byte_reads;
int RF_CAL_BYTES(uint8_t chip, uint8_t page, uint8_t reg, uint8_t *out, uint8_t cap)
{
	(void)chip; (void)page; (void)reg; (void)out;
	cal_byte_reads++;
	return cap;
}
static unsigned codec_route_writes;
static uint8_t hpl_gain;
bool RF_CODEC_WRITE(uint8_t page, uint8_t reg, uint8_t value)
{
	if (page == 1 && reg == 16) { hpl_gain = value; return true; }
	assert(page == 1 && reg == 52 && (value == 0x80 || value == 0x20));
	codec_route_writes++;
	return true;
}
static bool gps_started;
static bool analog_enabled, analog_tx;
void RF_ANALOG_REQUEST(bool enabled, bool tx, bool narrow)
{
	(void)narrow; analog_enabled = enabled; analog_tx = tx;
}
static bool codec_loopback;
uint32_t RF_ANALOG_VALUE(uint8_t key) { assert(key == 10); return codec_loopback; }
bool RF_MODEM_LOOPBACK(bool enabled) { codec_loopback = enabled; return true; }
static unsigned waits;
void hal_delay_us(uint32_t us) { (void)us; }
void hal_delay_ms(uint32_t ms)
{
	(void)ms;
	assert(!gps_started);
	waits++;
}
void RF_GPS_INIT(void)
{
	assert(waits != 0);
	assert(rf.bus != NULL && rf3w.bus.write != NULL);
	gps_started = true;
	// This used to call an uninitialized bus from the GPS task.
	RF_GPS_POWER(true);
	assert(((RF_GPIO_READ() >> 4) & 3u) == 3u);
	RF_GPS_POWER(false);
	assert(((RF_GPIO_READ() >> 4) & 3u) == 2u);
}
bool RF_CAL_DRIVE(uint8_t level, uint32_t hz, uint8_t *drive)
{
	(void)level; (void)hz;
	*drive = 0;
	return true;
}

static void test_audio_path(void)
{
	set_mode(RADIO_MODE_FM);
	assert(host_dac_init_mask == 1u && host_dac_writes[1] == 0);
	control_valid = true;
	saved_control = 3619;
	assert(rx(145500000u, RADIO_BW_12K5, NULL));
	assert(host_dac_values[1] == saved_control && host_dac_values[0] == 0);
	assert((rf.cache[0][0x3A] & 0x7000u) == 0x4000u);
	assert(rf.cache[0][0x44] == RF_AUDIO_TRIM_FM);
	assert(analog_enabled && !analog_tx);
	uint16_t input = rf.cache[0][0x0A], voice = rf.cache[0][0x41], deviation = rf.cache[0][0x59];
	saved_control = 3620;
	assert(tx(145500000u, RADIO_BW_12K5, 0, NULL));
	assert(host_dac_values[1] == saved_control);
	assert(rf.state == AT1846S_STATE_FM_TX);
	assert(analog_enabled && analog_tx);
	assert(rf.cache[0][0x30] == 0x4046 && rf.cache[0][0x58] == RF_AUDIO_58_FM_TX);
	assert(rf.cache[0][0x0A] == (uint16_t)(0x3BA0u | (input & 0xFu)));
	assert(rf.cache[0][0x44] == RF_TX_GAIN_FM && rf.cache[0][0x41] == RF_TX_VOICE_GAIN);
	assert(rf.cache[0][0x59] == RF_TX_DEVIATION);
	tx_off();
	assert(rf.cache[0][0x30] == 0x4026 && rf.cache[0][0x58] == RF_AUDIO_58_FM);
	assert(rf.cache[0][0x0A] == input && rf.cache[0][0x41] == voice && rf.cache[0][0x59] == deviation);
	assert(rf.state == AT1846S_STATE_RX);

	// Changing bandwidth after selecting DMR used to erase the board's mode/audio trims.
	set_mode(RADIO_MODE_DMR);
	assert(!analog_enabled);
	assert(rx(145500000u, RADIO_BW_25K, NULL));
	assert(rf.cache[0][0x44] == RF_AUDIO_TRIM_DMR);
	assert(rf.cache[0][0x40] == RF_AUDIO_FILTER_DMR);
	assert(rf.cache[0][0x58] == RF_AUDIO_58_DMR);
	assert((rf.cache[0][0x3A] & 0x7000u) == 0x4000u);
	assert(tx(438500000u, RADIO_BW_12K5, 0, NULL));
	assert(rf.state == AT1846S_STATE_DMR_TX);
	assert(rf.cache[0][0x30] == 0x4046);
	assert(rf.cache[0][0x44] == RF_TX_GAIN_DMR);
	assert(rf.cache[0][0x41] == RF_TX_VOICE_GAIN && rf.cache[0][0x59] == RF_TX_DEVIATION);
	tx_off();
	assert(rf.bw == AT1846S_BW_25K);
	uint32_t word = at1846s_freq_word(145500000u);
	assert(rf.cache[0][0x29] == (uint16_t)((word >> 16) & 0x3FFFu));
	assert(rf.cache[0][0x2A] == (uint16_t)word);
	assert(rf.state == AT1846S_STATE_RX);
	assert(rf.cache[0][0x30] == 0x7026);
	set_mode(RADIO_MODE_FM);
	assert(tx(433500000u, RADIO_BW_25K, 0, NULL));
	assert(rf.cache[0][0x30] == 0x7046 && rf.cache[0][0x58] == RF_AUDIO_58_FM_TX);
	tx_off();
	assert(rf.cache[0][0x58] == RF_AUDIO_58_FM);
	// Entering RX directly must also undo TX trims, including ones not rewritten by a cached mode table.
	assert(tx(433500000u, RADIO_BW_12K5, 0, NULL));
	assert(rx(433500000u, RADIO_BW_12K5, NULL));
	assert(rf.cache[0][0x41] == voice && rf.cache[0][0x59] == deviation);
	assert(rf.cache[0][0x44] == RF_AUDIO_TRIM_FM);
}

int main(void)
{
	assert(!gps_started);
	init();
	assert(gps_started);
	assert(host_dac_init_mask == 1u && host_dac_writes[1] == 0);
	assert(host_dac_values[0] == 0 && !keyed);
	assert(RF_RADIO.mic_gain != NULL);
	RF_RADIO.mic_gain(6);
	assert(mic_offset == 6);
	volume(20);
	audio(true);
	assert(audio_requested && !speaker_muted && !(hpl_gain & 0x40));
	volume(0);
	assert(audio_requested && speaker_muted && hpl_gain == 0x40);
	audio(true); // squelch opening cannot undo a zero-volume mute
	assert(speaker_muted && hpl_gain == 0x40);
	volume(20);
	assert(!speaker_muted && !(hpl_gain & 0x40));
	audio(false);
	test_audio_path();
	uint32_t route = 0x20;
	assert(!RF_AUDIO_REG(1, 1, 52, &route, false));
	assert(!RF_AUDIO_REG(1, 1, 51, &route, true));
	uint8_t bytes[2];
	keyed = true;
	assert(RF_CHIP_BYTES(3, 0, 0, bytes, sizeof(bytes)) == -1 && cal_byte_reads == 0);
	assert(!RF_AUDIO_REG(1, 1, 52, &route, true));
	keyed = false;
	assert(RF_CHIP_BYTES(3, 0, 0, bytes, sizeof(bytes)) == 2 && cal_byte_reads == 1);
	assert(RF_AUDIO_REG(1, 1, 52, &route, true));
	route = 0x80;
	assert(RF_AUDIO_REG(1, 1, 52, &route, true));
	route = 0xA0;
	assert(!RF_AUDIO_REG(1, 1, 52, &route, true));
	assert(codec_route_writes == 2);
	uint32_t loop = 0x11;
	assert(RF_AUDIO_REG(2, 0, 0x18, &loop, true));
	assert(codec_loopback);
	assert(!tx(433500000u, RADIO_BW_12K5, 0, NULL));
	assert(!keyed && rf.state == AT1846S_STATE_RX);
	loop = 2;
	assert(!RF_AUDIO_REG(2, 0, 0x18, &loop, true));
	loop = 0;
	assert(RF_AUDIO_REG(2, 0, 0x18, &loop, false) && loop == 0x11);
	loop = 0;
	assert(RF_AUDIO_REG(2, 0, 0x18, &loop, true) && !codec_loopback);
	puts("rfGpsStartupTest PASSED");
	return 0;
}
