// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Run the real RF initialization, scheduling GPS power immediately when its task is created.
#include <assert.h>
#include <stdio.h>
#include RF_STARTUP_SOURCE

unsigned host_dac_init_mask, host_dac_writes[2];
uint16_t host_dac_values[2];
uint32_t host_dac_control, host_dac_clock;
unsigned host_af_mask[5], host_timer_clocks;
unsigned host_pin_levels[5];
uint32_t host_timer_regs[6][16];
static bool tune_valid;
bool RF_CAL_TUNE(uint32_t hz, uint16_t *duty)
{
	*duty = hz >= 400000000u ? 1972u : 1644u;
	return tune_valid;
}
uint16_t RF_CAL_MOD_GAIN(uint32_t hz) { (void)hz; return 3500u; }
static uint32_t rf_deviation_hz;
static uint8_t factory_rf_type = RF_KIND_N;
uint8_t RF_CAL_TYPE(void) { return factory_rf_type; }
uint16_t RF_CAL_DEVIATION(uint32_t hz, bool dcs)
{
	rf_deviation_hz = hz; return dcs ? 0x0A8Cu : 0x0A92u;
}
static uint32_t modulation_hz;
void RF_MODEM_DEVIATION(uint32_t hz) { modulation_hz = hz; }
static bool mic_prepared;
void RF_MODEM_PA_ARM(void) { mic_prepared = false; }
bool RF_MODEM_PA_READY(void) { return mic_prepared; }
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
	*drive = 47;
	return true;
}

static void test_revision_profiles(void)
{
	assert(rf.cache[0][0x0F] == 0x8C24 && rf.cache[0][0x13] == 0xB100);
	assert(rf.cache[0][0x54] == 0x2A18 && rf.cache[0][0x5A] == 0x0CDD && rf.cache[0][0x63] == 0x3FFF);
	assert(rx(250000000u, RADIO_BW_25K, NULL));
	assert(rf.cache[0][0x32] == 0x75E4 && rf.cache[0][0x34] == 0x2B8B);
	assert(tx(433500000u, RADIO_BW_25K, 0, NULL));
	assert(rf.cache[0][0x32] == 0x75E4 && rf.cache[0][0x34] == 0x2B8B); // width follows RX on a split
	assert(rf_deviation_hz == 433500000u); // final deviation follows TX
	tx_off();
	assert(rx(433500000u, RADIO_BW_25K, NULL));
	assert(rf.cache[0][0x32] == 0x75E3 && rf.cache[0][0x34] == 0x2B89);
	assert(rf.cache[0][0x3F] == 0x29D2 && rf.cache[0][0x60] == 0x203E && rf.cache[0][0x62] == 0x2346);
	assert(rx(145500000u, RADIO_BW_12K5, NULL));
	assert(rf.cache[0][0x33] == 0x4493 && rf.cache[0][0x34] == 0x2B8E);
	assert(rf.cache[0][0x3C] == 0x0408 && rf.cache[0][0x3F] == 0x28D0);
	assert(rf.cache[0][0x60] == 0x1CB8 && rf.cache[0][0x62] == 0x0A10);
	assert(rf.cache[1][7] == 0x020C && rf.cache[1][0x12] == 0xE0EB);
	factory_rf_type = RF_KIND_OTHER; configure_profile(145500000u);
	assert(rf.profile == NULL);
	assert(at1846s_set_bandwidth(&rf, AT1846S_BW_12K5));
	assert(rf.cache[0][0x3C] == 0x1B34); // other/unknown identities retain the existing profile
	factory_rf_type = RF_KIND_UNKNOWN; configure_profile(145500000u);
	assert(rf.profile == NULL);
	factory_rf_type = RF_KIND_N; configure_profile(145500000u);
}

static void test_audio_path(void)
{
	set_mode(RADIO_MODE_FM);
	tune_pwm(145500000u);
	assert(host_timer_clocks == 0 && !tune_initialized); // absent calibration must not enable the output
	tune_valid = true;
	assert(host_dac_init_mask == 1u && host_dac_writes[1] == 0);
	control_valid = true;
	saved_control = 3619;
	assert(rx(145500000u, RADIO_BW_12K5, NULL));
	// The tuning pin belongs to the timer at this address; its clock, channel and compare must agree.
	assert(TUNE_TIMER == 0x40000400u && host_timer_clocks == 2u);
	assert(TIMER_CAR(0x40000400u) == 4095 && TIMER_CTL0(0x40000400u) == 1);
	assert(TIMER_CHCTL0(0x40000400u) == 0x68 && TIMER_CHCTL2(0x40000400u) == 1);
	assert(TIMER_CH0CV(0x40000400u) == 1644 && (host_af_mask[GPIOA] & MCU_PIN(6)));
	assert(RF_TUNING_VALUE(0) == 1 && RF_TUNING_VALUE(1) == 1644 && RF_TUNING_VALUE(2) == 1644);
	assert(host_dac_values[1] == saved_control && host_dac_values[0] == 0);
	assert(host_dac_control == 0x00030003u); // both channels enabled, buffers off, triggers/waves/DMA off
	assert((rf.cache[0][0x3A] & 0x7000u) == 0x4000u);
	assert(rf.cache[0][0x44] == RF_AUDIO_TRIM_FM);
	assert(analog_enabled && !analog_tx);
	assert(modulation_hz == 145500000u);
	uint16_t input = rf.cache[0][0x0A], voice = rf.cache[0][0x41], deviation = rf.cache[0][0x59];
	saved_control = 3620;
	assert(tx(145500000u, RADIO_BW_12K5, 0, NULL));
	assert(host_dac_values[1] == saved_control);
	assert(rf.state == AT1846S_STATE_FM_TX);
	assert(analog_enabled && analog_tx);
	assert(rf.cache[0][0x30] == 0x4046 && rf.cache[0][0x58] == RF_AUDIO_58_FM_TX);
	assert(rf.cache[0][0x0A] == (uint16_t)(0x3BA0u | (input & 0xFu)));
	assert(rf.cache[0][0x44] == RF_TX_GAIN_FM && rf.cache[0][0x41] == RF_TX_VOICE_GAIN);
	assert(rf.cache[0][0x59] == (mode_now == RADIO_MODE_FM ? 0x0A92u : RF_TX_DEVIATION));
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
	host_pin_levels[GPIOD] = MCU_PIN(0);
	mic_prepared = true; // an old call's completed setup must not arm a fresh call
	assert(tx(438500000u, RADIO_BW_12K5, 0, NULL));
	assert(!mic_prepared && host_dac_values[0] == 0);
	MCU_EXTI0_HANDLER();
	assert(host_dac_values[0] == 0);
	mic_prepared = true;
	MCU_EXTI0_HANDLER();
	assert(host_dac_values[0] != 0);
	host_pin_levels[GPIOD] = 0;
	MCU_EXTI0_HANDLER();
	assert(host_dac_values[0] == 0);
	assert(TIMER_CH0CV(0x40000400u) == 1972 && host_timer_clocks == 2u);
	assert(rf.state == AT1846S_STATE_DMR_TX);
	assert(rf.cache[0][0x30] == 0x4046);
	assert(rf.cache[0][0x44] == RF_TX_GAIN_DMR);
	assert(rf.cache[0][0x41] == RF_TX_VOICE_GAIN && rf.cache[0][0x59] == RF_TX_DEVIATION);
	tx_off();
	assert(rf.bw == AT1846S_BW_25K);
	assert(TIMER_CH0CV(0x40000400u) == 1644); // split TX must restore receive tracking as well as frequency
	uint32_t word = at1846s_freq_word(145500000u);
	assert(rf.cache[0][0x29] == (uint16_t)((word >> 16) & 0x3FFFu));
	assert(rf.cache[0][0x2A] == (uint16_t)word);
	assert(rf.state == AT1846S_STATE_RX);
	assert(rf.cache[0][0x30] == 0x7026);
	set_mode(RADIO_MODE_FM);
	assert(tx(433500000u, RADIO_BW_25K, 0, NULL));
	assert(modulation_hz == 433500000u);
	assert(rf.cache[0][0x30] == 0x7046 && rf.cache[0][0x58] == RF_AUDIO_58_FM_TX);
	tx_off();
	assert(rf.cache[0][0x58] == RF_AUDIO_58_FM);
	assert(modulation_hz == 145500000u);
	uint16_t fm_rx_voice = rf.cache[0][0x41], fm_rx_deviation = rf.cache[0][0x59];
	// Entering RX directly must also undo TX trims, including ones not rewritten by a cached mode table.
	assert(tx(433500000u, RADIO_BW_12K5, 0, NULL));
	assert(rx(433500000u, RADIO_BW_12K5, NULL));
	assert(rf.cache[0][0x41] == fm_rx_voice && rf.cache[0][0x59] == fm_rx_deviation);
	assert(rf.cache[0][0x44] == RF_AUDIO_TRIM_FM);
	// Tone selection precedes the calibrated final deviation; a split uses its TX frequency.
	const subtone_t tones[] = { { TONE_CTCSS, 885 }, { TONE_DCS, 23 }, { TONE_DCS_INV, 23 } };
	for (unsigned i = 0; i < sizeof(tones) / sizeof(tones[0]); i++) {
		assert(tx(438500000u, RADIO_BW_12K5, 0, &tones[i]));
		assert(rf.cache[0][0x59] == (i == 0 ? 0x0A92u : 0x0A8Cu));
		assert(rf_deviation_hz == 438500000u);
		tx_off();
		assert(rf.cache[0][0x59] == fm_rx_deviation);
	}
}

int main(void)
{
	assert(!gps_started);
	const uint32_t initial_modes[] = { 0, UINT32_MAX };
	for (unsigned i = 0; i < 2; i++) {
		gps_started = false;
		host_dac_control = initial_modes[i]; // cold reset and stale trigger/wave/DMA settings
		init();
		assert(host_dac_clock & RCU_APB1EN_DACEN);
		assert(host_dac_control == 0x00020003u); // PA5 stays disabled until calibration is available
	}
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
	test_revision_profiles();
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
