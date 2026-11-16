// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS RF: one AT1846S on a bit-banged three-wire bus (docs/spec/hardware-d878uv2.md 4.1, 4.3).
//
// The chip's own register tables live in drivers/rf/at1846s. What this board adds is the bus, the band and
// front-end switching, and the transmitter's gate and level.
//
// **The band switching is on the RF chip's GPIO pins, not the MCU's.** The firmware drives them as plain outputs
// through the chip's register 0x1F, two bits per pin, keeping a shadow copy and rewriting the whole register for
// every change (4.3). Receive and transmit are switched in three groups, not by one T/R line:
//   - the two receive band enables, gpio0 and gpio4, complementary at 300 MHz, and BOTH low while transmitting;
//   - the PA gate, PC4 with PE6, which then keys per TDMA slot from PD0 while DMR runs;
//   - the transmit band select, gpio5, with gpio7 for the 165-300 MHz path the stock firmware never keys.
//
// Transmit drive comes from the per-MHz tables in the radio's own NAND (5). Unavailable calibration uses the
// existing low drive floor; calibrated RF operation still needs owner testing (docs/testing/d878uv2-bringup.md).
#include "d878uv2.h"
#include "config.h"
#include "pins.h"
#include "mcu_io.h"
#include "hal_time.h"
#include "rf/at1846s/at1846s.h"
#include "rf/rfbus/rfbus.h"

// Above this the radio is on its UHF path, at or below on VHF (4.3: a plain constant in the stock firmware)
#define BAND_SPLIT_HZ		300000000u
// gpio7's window, the transmit path the stock firmware never offers
#define GPIO7_LOW_HZ		165000000u
#define GPIO7_HIGH_HZ		300000000u

// Register 0x1F, two bits per pin: 00 high impedance, 01 the pin's built-in function, 10 drive low, 11 drive high
#define RF_GPIO_REG		0x1Fu
#define GPIO_HIZ		0u
#define GPIO_FUNC		1u
#define GPIO_LOW		2u
#define GPIO_HIGH		3u
// The value the stock firmware writes when it sets the chip up (4.3)
#define RF_GPIO_SETUP		0x5AE2u

static at1846s_t rf;
// This RF path uses the microphone pin for both modes; its TX state must not mute that path.
static const uint16_t control_values[][2] = {
	[AT1846S_STATE_OFF] = { 0x4006, 0x7006 },
	[AT1846S_STATE_IDLE] = { 0x4006, 0x7006 },
	[AT1846S_STATE_RX] = { 0x4026, 0x7026 },
	[AT1846S_STATE_FM_TX] = { 0x4046, 0x7046 },
	[AT1846S_STATE_DMR_TX] = { 0x4046, 0x7046 },
};
static rfbus_3w_t rf3w;
static uint16_t gpio_shadow = RF_GPIO_SETUP;
static bool keyed;
static radio_bw_t rx_bw = RADIO_BW_12K5;
static uint32_t rx_hz;		// the last frequency tuned for receive, to put the front end back after a transmission

// --- The three-wire bus ---

static void bus_cs(bool level)
{
	mcu_pin_write(PIN_RF_CS, level);
}

static void bus_clk(bool level)
{
	mcu_pin_write(PIN_RF_SCLK, level);
}

static void bus_data_out(bool level)
{
	mcu_pin_out(PIN_RF_SDIO, level, false);
}

static void bus_data_release(void)
{
	mcu_pin_in(PIN_RF_SDIO, MCU_PULL_UP);
}

static bool bus_data_in(void)
{
	return mcu_pin_read(PIN_RF_SDIO);
}

static const rfbus_3w_pins_t bus_pins = {
	.cs = bus_cs,
	.clk = bus_clk,
	.data_out = bus_data_out,
	.data_release = bus_data_release,
	.data_in = bus_data_in,
	.delay_us = hal_delay_us,
	.delay_ms = hal_delay_ms,
};

static bool rf_write(void *ctx, uint8_t reg, uint16_t val)
{
	(void)ctx;
	rfbus_write(&rf3w.bus, reg, val);
	return true;
}

static bool rf_read(void *ctx, uint8_t reg, uint16_t *val)
{
	(void)ctx;
	*val = rfbus_read(&rf3w.bus, reg);
	return true;
}

static const at1846s_bus_t rf_bus = { .write = rf_write, .read = rf_read, .delay_ms = hal_delay_ms, .ctx = NULL };

// --- The chip's GPIO pins: band and front-end switching ---

static void gpio_field(unsigned pin, unsigned mode)
{
	unsigned shift = pin * 2u;
	gpio_shadow = (uint16_t)((gpio_shadow & ~(3u << shift)) | ((uint16_t)mode << shift));
}

static void gpio_flush(void)
{
	at1846s_write_force(&rf, 0, RF_GPIO_REG, gpio_shadow);
}

// Receiving: one band enable on, the other off
static void front_end_rx(uint32_t hz)
{
	bool uhf = hz > BAND_SPLIT_HZ;
	gpio_field(0, uhf ? GPIO_HIGH : GPIO_LOW);
	gpio_field(4, uhf ? GPIO_LOW : GPIO_HIGH);
	gpio_field(7, GPIO_LOW);	// the second transmit path is held off in receive
	gpio_flush();
}

// Transmitting: both receive enables off, then the transmit path picked
static void front_end_tx(uint32_t hz)
{
	gpio_field(0, GPIO_LOW);
	gpio_field(4, GPIO_LOW);
	gpio_field(5, hz > BAND_SPLIT_HZ ? GPIO_HIGH : GPIO_LOW);
	gpio_field(7, (hz >= GPIO7_LOW_HZ && hz <= GPIO7_HIGH_HZ) ? GPIO_HIGH : GPIO_LOW);
	gpio_flush();
}

#if CONFIG_GPS
// The GPS module's supply is the RF chip's gpio2, not an MCU pin (hardware-d878uv2.md 4.3)
void d878uv2_gps_power(bool on)
{
	gpio_field(2, on ? GPIO_HIGH : GPIO_LOW);
	gpio_flush();
}
#endif

// The shadow as last written, for the diagnostics line: which front-end path is enabled
uint16_t d878uv2_rf_gpio(void)
{
	return gpio_shadow;
}

// --- The transmitter's gate and level ---

// The drive comes from the radio's own per-MHz calibration (cal_d878uv2.c). Without it the transmitter stays at
// the floor below rather than guessing: a wrong drive is what damages a PA. Only the top 8 bits of the DAC are
// used (4.3)
#define TX_DRIVE_UNCALIBRATED	0x20u

static bool control_initialized;
static void control_dac(void)
{
	uint16_t value;
	if (!d878uv2_cal_control(&value)) return;
	if (!control_initialized) {
		mcu_dac_init(PIN_CONTROL_DAC);
		control_initialized = true;
	}
	mcu_dac_write(PIN_CONTROL_DAC, value);
}

static void tx_dac(uint8_t level8)
{
	mcu_dac_write(0, (uint16_t)level8 << 4);
}

static void pa_on(uint8_t power, uint32_t hz)
{
	uint8_t drive = TX_DRIVE_UNCALIBRATED;

	if (!d878uv2_cal_tx_drive(power, hz, &drive)) {
		drive = TX_DRIVE_UNCALIBRATED;
	}
	mcu_pin_write(PIN_PA_EN0, true);
	mcu_pin_write(PIN_PA_EN1, true);
	tx_dac(drive);
	keyed = true;
}

static void pa_off(void)
{
	tx_dac(0);
	mcu_pin_write(PIN_PA_EN1, false);
	mcu_pin_write(PIN_PA_EN0, false);
	keyed = false;
}

// board_rf_safe's part: the gate down, the level at zero, both receive enables off. Plain writes only, so it is
// safe from a fault handler
void d878uv2_rf_safe(void)
{
	tx_dac(0);
	mcu_pin_write(PIN_PA_EN1, false);
	mcu_pin_write(PIN_PA_EN0, false);
	keyed = false;
}

// --- hal_radio_t ---

static void init(void)
{
	mcu_pin_out(PIN_RF_CS, true, false);
	mcu_pin_out(PIN_RF_SCLK, false, false);
	mcu_pin_out(PIN_RF_SDIO, false, false);
	mcu_pin_out(PIN_PA_EN0, false, false);
	mcu_pin_out(PIN_PA_EN1, false, false);
	mcu_pin_out(PIN_SPK_EN, false, false);
	mcu_pin_out(PIN_AUDIO_SW, false, false);
	// Both DAC outputs: PA4 the transmit power level, **PA5 the frequency trim**. A DAC pin has to be in analog
	// mode or its output buffer never properly reaches the pin, so writing the calibration count alone did
	// nothing - which is why the radio ran with no frequency calibration at all
	// (docs/spec/hardware-d878uv2.md 5.1.1, the service screen's FQCU)
	mcu_pin_analog(GPIOA, MCU_PIN(4) | MCU_PIN(5));
	mcu_dac_init(0);
	tx_dac(0);
	control_dac();

	rfbus_3w_attach(&rf3w, &bus_pins);
	at1846s_attach(&rf, &rf_bus);
	rf.control_values = control_values;
	at1846s_init(&rf);
	gpio_shadow = RF_GPIO_SETUP;
	gpio_flush();
	at1846s_init_finish(&rf);
#if CONFIG_GPS
	// GPS power is an RF-chip GPIO: its task must not run until this bus is ready.
	d878uv2_gps_init();
#endif
}

static radio_mode_t mode_now = RADIO_MODE_FM;

// The RF chip's audio registers, per mode (hardware-d878uv2.md 4.6). 0x44's bits 7:4 are full AF gain in both
// modes; its bits 11:8 are a fixed trim, one of five FM steps in the stock firmware chosen by a settings byte -
// the middle one is used here. 0x40 bit 0 passes the low-frequency audio 4FSK needs
#define RF_AUDIO_TRIM_FM	0x09F9u
#define RF_AUDIO_TRIM_DMR	0x0AFAu
#define RF_AUDIO_FILTER_FM	0x0030u
#define RF_AUDIO_FILTER_DMR	0x0031u
#define RF_AUDIO_58_FM		0x8085u
#define RF_AUDIO_58_FM_TX	0x8405u
#define RF_AUDIO_58_DMR		0x80E5u
#define RF_TX_GAIN_FM		0x09FFu	// documented reset index 2 of the TX table
#define RF_TX_GAIN_DMR		0x0BFFu
#define RF_TX_VOICE_GAIN	0x070Fu
#define RF_TX_DEVIATION		0x0C50u

static struct {
	uint16_t input, voice, deviation;
	bool valid;
} rx_audio;

static bool restore_rx_audio(void)
{
	if (!rx_audio.valid) return true;
	bool ok = at1846s_write(&rf, 0, 0x0A, rx_audio.input);
	ok &= at1846s_write(&rf, 0, 0x41, rx_audio.voice);
	ok &= at1846s_write(&rf, 0, 0x59, rx_audio.deviation);
	if (ok) rx_audio.valid = false;
	return ok;
}

static bool apply_tx_audio(void)
{
	// These registers were initialized by the RF driver. Save their RX values before temporary TX trims.
	if (!rx_audio.valid) {
		rx_audio.input = rf.cache[0][0x0A];
		rx_audio.voice = rf.cache[0][0x41];
		rx_audio.deviation = rf.cache[0][0x59];
		rx_audio.valid = true;
	}
	bool ok = true;
	if (mode_now == RADIO_MODE_FM) {
		ok &= at1846s_write(&rf, 0, 0x58, RF_AUDIO_58_FM_TX);
		ok &= at1846s_write(&rf, 0, 0x40, RF_AUDIO_FILTER_FM);
	}
	ok &= at1846s_modify(&rf, 0, 0x0A, 0x87FFu, 0x3800u);
	ok &= at1846s_modify(&rf, 0, 0x0A, 0x780Fu, 0x03A0u);
	ok &= at1846s_write(&rf, 0, 0x44, mode_now == RADIO_MODE_FM ? RF_TX_GAIN_FM : RF_TX_GAIN_DMR);
	ok &= at1846s_write(&rf, 0, 0x41, RF_TX_VOICE_GAIN);
	ok &= at1846s_write(&rf, 0, 0x59, RF_TX_DEVIATION);
	return ok;
}

// Bandwidth tables overwrite mode and audio registers, so restore this board's wiring after them.
static bool apply_mode(void)
{
	bool dmr = mode_now == RADIO_MODE_DMR;
	bool ok = at1846s_set_mode(&rf, dmr ? AT1846S_MODE_DMR : AT1846S_MODE_FM);
	ok &= at1846s_write(&rf, 0, 0x58, dmr ? RF_AUDIO_58_DMR : RF_AUDIO_58_FM);
	ok &= at1846s_write(&rf, 0, 0x40, dmr ? RF_AUDIO_FILTER_DMR : RF_AUDIO_FILTER_FM);
	ok &= at1846s_write(&rf, 0, 0x44, dmr ? RF_AUDIO_TRIM_DMR : RF_AUDIO_TRIM_FM);
	// The codec's modulator outputs are wired to the RF chip's microphone input (spec 4.6.4).
	ok &= at1846s_modify(&rf, 0, 0x3A, 0x8FFFu, 0x4000u);
	return ok;
}

static void set_mode(radio_mode_t mode)
{
	mode_now = mode;
#if CONFIG_FM
	if (mode != RADIO_MODE_FM) {
		d878uv2_modem_analog(false, false, true);
	}
#endif
	(void)apply_mode();
}

static at1846s_bw_t bw_of(radio_bw_t bw)
{
	return bw == RADIO_BW_25K ? AT1846S_BW_25K : AT1846S_BW_12K5;
}

// The core's sub-audio types onto the chip driver's
static at1846s_tone_t tone_of(tone_type_t t)
{
	switch (t) {
	case TONE_CTCSS:
		return AT1846S_TONE_CTCSS;
	case TONE_DCS:
		return AT1846S_TONE_DCS;
	case TONE_DCS_INV:
		return AT1846S_TONE_DCS_INV;
	default:
		return AT1846S_TONE_NONE;
	}
}

static bool rx(uint32_t hz, radio_bw_t bw, const subtone_t *rx_tone)
{
	pa_off();
	bool ok = restore_rx_audio();
	ok &= at1846s_set_bandwidth(&rf, bw_of(bw));
	ok &= apply_mode();
	control_dac();
	ok &= at1846s_set_freq(&rf, hz);
	rx_hz = hz;
	rx_bw = bw;
	front_end_rx(hz);
	if (rx_tone != NULL && rx_tone->type != TONE_NONE) {
		ok &= at1846s_set_rx_tone(&rf, tone_of(rx_tone->type), rx_tone->value);
	} else {
		ok &= at1846s_set_rx_tone(&rf, AT1846S_TONE_NONE, 0);
	}
	ok &= at1846s_set_state(&rf, AT1846S_STATE_RX);
#if CONFIG_FM
	if (mode_now == RADIO_MODE_FM) d878uv2_modem_analog(true, false, bw != RADIO_BW_25K);
#endif
	return ok;
}

static bool tx(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tx_tone)
{
#if CONFIG_DIAG && CONFIG_FM
	// A codec loopback test must not key the PA, even if PTT is pressed.
	if (d878uv2_analog_value(10)) return false;
#endif
	bool ok = at1846s_set_state(&rf, AT1846S_STATE_IDLE);
	ok &= at1846s_set_bandwidth(&rf, bw_of(bw));
	ok &= apply_mode();
	control_dac();
	ok &= at1846s_set_freq(&rf, hz);
	if (tx_tone != NULL && tx_tone->type != TONE_NONE) {
		ok &= at1846s_set_tx_tone(&rf, tone_of(tx_tone->type), tx_tone->value);
	} else {
		ok &= at1846s_set_tx_tone(&rf, AT1846S_TONE_NONE, 0);
	}
	ok &= apply_tx_audio();
	front_end_tx(hz);
	ok &= at1846s_set_state(&rf, mode_now == RADIO_MODE_DMR ? AT1846S_STATE_DMR_TX : AT1846S_STATE_FM_TX);
#if CONFIG_FM
	if (mode_now == RADIO_MODE_FM) d878uv2_modem_analog(true, true, bw != RADIO_BW_25K);
#endif
	pa_on(power, hz);
	return ok;
}

// Back to receive. **The front end has to be put back**: keying drove both receive band enables low (4.3), and
// leaving them there makes the radio deaf after every transmission - the chip reads no signal at all and the
// stack sees a dead channel. Measured on a radio before this: RSSI pinned at its floor directly after a
// transmission until the channel was changed
static void tx_off(void)
{
	pa_off();
	at1846s_set_state(&rf, AT1846S_STATE_IDLE);
	(void)restore_rx_audio();
	if (rx_hz != 0) {
		at1846s_set_bandwidth(&rf, bw_of(rx_bw));
		(void)apply_mode();
		control_dac();
		at1846s_set_freq(&rf, rx_hz);
		front_end_rx(rx_hz);
		at1846s_set_state(&rf, AT1846S_STATE_RX);
#if CONFIG_FM
		if (mode_now == RADIO_MODE_FM) d878uv2_modem_analog(true, false, rx_bw != RADIO_BW_25K);
#endif
	}
}

// The chip's reading is its own scale. The offset below is a datasheet-order placeholder: two known levels on the
// bench set it (docs/testing/d878uv2-bringup.md, stage 4)
#define RSSI_DBM_OFFSET		(-137)

static void status(radio_status_t *st)
{
	uint8_t rssi_db = 0, noise_db = UINT8_MAX;
	uint16_t flags = 0;
	st->rssi_dbm = -160;
	st->noise = UINT8_MAX;
	st->carrier = false;
	st->tone_ok = true;
	if (at1846s_read_signal(&rf, &rssi_db, &noise_db)) {
		st->rssi_dbm = (int16_t)((int)rssi_db + RSSI_DBM_OFFSET);
		st->noise = noise_db;
	}
	if (at1846s_read_flags(&rf, &flags)) {
		st->carrier = (flags & AT1846S_FLAG_SQ) != 0;
	}
#if CONFIG_DMR
	// The baseband hunts for sync only once it has been told the receiver is ready (modem-sct.md 3)
	if (mode_now == RADIO_MODE_DMR) {
		d878uv2_modem_carrier(st->carrier);
	}
#endif
}

// The speaker amplifier and what it listens to. **PE0 is the amplifier's input select, not the inverse of PA7**:
// high picks the codec's output (the receive audio of both modes), low the other analog source, which is the tone
// generator on PB9 and the broadcast receiver. Stock receive audio is both pins high together; the earlier
// reading of "driven opposite to PA7" came from the broadcast paths, the only ones that pair PE0 low with PA7
// high (docs/spec/hardware-d878uv2.md 4.3, 4.6). Driving PE0 low here pointed the amplifier at the tone input and
// was why a receiver that opened its squelch was silent
static bool audio_requested, speaker_muted = true;

static void audio(bool on)
{
	audio_requested = on;
	on = on && !speaker_muted;
	mcu_pin_write(PIN_AUDIO_SW, on);	// the codec's output
	mcu_pin_write(PIN_SPK_EN, on);
}

#if CONFIG_DMR || CONFIG_FM
// The volume is made in the codec, not in the RF chip: the TLV320AIC3204's HPL driver gain (page 1, register
// 0x10), six bits of two's complement dB from -6 to +29, with bit 6 the mute. Every gain write clears that bit,
// which is the only unmute route there is. The RF chip's 0x44 is a fixed per-mode trim and the knob never reaches
// it (docs/spec/hardware-d878uv2.md 4.6).
//
// The stock firmware maps the knob through a per-mode curve table this port does not have, so the mapping below
// is a plain line across the same range. It is a placeholder: the levels will not match the stock radio's
#define CODEC_PAGE_OUT		1u
#define CODEC_HPL_GAIN		0x10u
#define CODEC_GAIN_MIN_DB	(-6)
#define CODEC_GAIN_MAX_DB	29

static void mic_gain(int8_t db)
{
	d878uv2_modem_mic_gain(db);
}

static void volume(uint8_t percent)
{
	int db = CODEC_GAIN_MIN_DB + ((int)percent * (CODEC_GAIN_MAX_DB - CODEC_GAIN_MIN_DB)) / 100;
	uint8_t reg = (uint8_t)((db < 0 ? db + 64 : db) & 0x3F);	// two's complement in six bits, mute clear

	speaker_muted = percent == 0;
	if (speaker_muted) reg = 0x40; // HPL driver mute, not its minimum unmuted gain
	audio(audio_requested);
	d878uv2_codec_write(CODEC_PAGE_OUT, CODEC_HPL_GAIN, reg);
}

#endif

#if CONFIG_DIAG
int d878uv2_chip_bytes(uint8_t chip, uint8_t page, uint8_t reg, uint8_t *out, uint8_t cap)
{
	if (keyed) return -1;
	return d878uv2_cal_bytes(chip, page, reg, out, cap);
}
#endif

#if CONFIG_DIAG && (CONFIG_DMR || CONFIG_FM)
// A write-only bring-up selector, not codec readback. Only the documented microphone inputs are allowed.
bool d878uv2_audio_reg(uint8_t chip, uint8_t page, uint8_t reg, uint32_t *value, bool write)
{
#if CONFIG_FM
	// Chip 2 exposes the SCT WORK_MODE loop byte, restricted to normal or codec loopback.
	if (chip == 2u && page == 0u && reg == 0x18u && value != NULL) {
		if (!write) {
			*value = d878uv2_analog_value(10) ? 0x11u : 0u;
			return true;
		}
		if (keyed || (*value != 0u && *value != 0x11u)) return false;
		return d878uv2_modem_loopback(*value == 0x11u);
	}
#endif
	if (!write || keyed || chip != 1u || page != 1u || reg != 0x34u || value == NULL ||
	    (*value != 0x80u && *value != 0x20u)) {
		return false;
	}
	return d878uv2_codec_write(page, reg, (uint8_t)*value);
}
#endif

const hal_radio_t d878uv2_radio = {
	.init = init,
	.set_mode = set_mode,
	.rx = rx,
	.tx = tx,
	.tx_off = tx_off,
	.status = status,
	.audio = audio,

#if CONFIG_DMR || CONFIG_FM
	.volume = volume,
	.mic_gain = mic_gain,
#endif
};
