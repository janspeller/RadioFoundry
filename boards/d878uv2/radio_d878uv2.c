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
// **The transmit level is not calibrated.** The real value comes from the per-MHz tables in the radio's own NAND
// (5), which no reader has been written for yet, so the table below is a deliberately low placeholder. Do not
// transmit into an antenna until it has been checked on a power meter
// (docs/testing/d878uv2-bringup.md, stage 7).
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
static rfbus_3w_t rf3w;
static uint16_t gpio_shadow = RF_GPIO_SETUP;
static bool keyed;

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
	mcu_pin_analog(GPIOA, MCU_PIN(4));	// the TX power DAC
	mcu_dac_init(0);
	tx_dac(0);

	rfbus_3w_attach(&rf3w, &bus_pins);
	at1846s_attach(&rf, &rf_bus);
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

static void set_mode(radio_mode_t mode)
{
	mode_now = mode;
	at1846s_set_mode(&rf, mode == RADIO_MODE_DMR ? AT1846S_MODE_DMR : AT1846S_MODE_FM);
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
	bool ok = at1846s_set_bandwidth(&rf, bw_of(bw));
	ok &= at1846s_set_freq(&rf, hz);
	front_end_rx(hz);
	if (rx_tone != NULL && rx_tone->type != TONE_NONE) {
		ok &= at1846s_set_rx_tone(&rf, tone_of(rx_tone->type), rx_tone->value);
	} else {
		ok &= at1846s_set_rx_tone(&rf, AT1846S_TONE_NONE, 0);
	}
	ok &= at1846s_set_state(&rf, AT1846S_STATE_RX);
	return ok;
}

static bool tx(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tx_tone)
{
	bool ok = at1846s_set_state(&rf, AT1846S_STATE_IDLE);
	ok &= at1846s_set_bandwidth(&rf, bw_of(bw));
	ok &= at1846s_set_freq(&rf, hz);
	if (tx_tone != NULL && tx_tone->type != TONE_NONE) {
		ok &= at1846s_set_tx_tone(&rf, tone_of(tx_tone->type), tx_tone->value);
	} else {
		ok &= at1846s_set_tx_tone(&rf, AT1846S_TONE_NONE, 0);
	}
	front_end_tx(hz);
	ok &= at1846s_set_state(&rf, AT1846S_STATE_FM_TX);
	pa_on(power, hz);
	return ok;
}

static void tx_off(void)
{
	pa_off();
	at1846s_set_state(&rf, AT1846S_STATE_IDLE);
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

// The speaker amplifier, with the second audio switch driven the other way in the receive path (4.3)
static void audio(bool on)
{
	mcu_pin_write(PIN_SPK_EN, on);
	mcu_pin_write(PIN_AUDIO_SW, !on);
}

// The chip's own volume register (AT1846S datasheet 0x44: two 4-bit fields). Unconfirmed on this radio
#define RF_VOLUME_REG		0x44u

static void volume(uint8_t percent)
{
	uint16_t step = (uint16_t)((percent * 15u) / 100u);
	at1846s_write(&rf, 0, RF_VOLUME_REG, (uint16_t)((step << 4) | step));
}

const hal_radio_t d878uv2_radio = {
	.init = init,
	.set_mode = set_mode,
	.rx = rx,
	.tx = tx,
	.tx_off = tx_off,
	.status = status,
	.audio = audio,
	.volume = volume,
};
