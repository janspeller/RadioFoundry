// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS power hold, power key, battery and backlight (docs/spec/hardware-d878uv2.md 4.3).
//
// A handheld on a 2-cell pack (the QB-44HL is 7.4 V): PC3 holds the radio on and dropping it powers it down, so
// "off" is really off, not the standby a radio on a fixed supply falls back to. The battery sits on ADC1
// channel 11 (PC1) behind about a
// 1:4 divider, and a panel potentiometer on channel 12 (PC2) is most likely the volume.
//
// **Two things to check on the bench before anything else** (docs/testing/d878uv2-bringup.md, stage 3):
//   - the power key's polarity. The stock firmware runs its shutdown chain while PA0 reads *high*, so this reads a
//     press as high. If that is backwards the radio powers itself off the moment it boots;
//   - the volume pot's travel. The stock firmware maps it through a table with hysteresis; the plain linear map
//     here is a placeholder, and its ends are unrecorded.
#include "d878uv2.h"
#include "pins.h"
#include "mcu_io.h"
#include "config.h"
#include "hal_time.h"
#include "radio/power_key.h"

#define BATT_SAMPLES	4u
// mV per count: the stock firmware averages four samples and scales by 330 * 4 / 4096 into units of 10 mV, so full
// scale is about 13.2 V and the divider about 1:4 (hardware-d878uv2.md 4.3)
#define BATT_MV_NUM	13200u
#define BATT_MV_DEN	4096u

static pwrkey_t power_key;

// 2S Li-ion, resting voltage to charge (per cell 4.10 V full .. 3.20 V empty, doubled), as the other 2-cell radios
// use. The stock firmware's own low warnings are at 6.10 V and 6.00 V, which sit just under this curve's floor
static const struct { uint16_t mv; uint8_t pct; } curve[] = {
	{ 6400, 0 }, { 6800, 4 }, { 7000, 10 }, { 7200, 22 }, { 7400, 42 },
	{ 7600, 60 }, { 7800, 75 }, { 8000, 88 }, { 8200, 100 },
};

static void init(void)
{
	mcu_pin_out(PIN_POWER_HOLD, true, false);
	mcu_pin_in(PIN_POWER_KEY, MCU_PULL_UP);
	mcu_pin_analog(GPIOC, MCU_PIN(0) | MCU_PIN(1) | MCU_PIN(2));
	mcu_adc_init();
	// The three indicator outputs, all off (hardware-d878uv2.md 4.3)
	mcu_pin_out(PIN_LED_RX, false, false);
	mcu_pin_out(PIN_LED_BT, false, false);
	mcu_pin_out(PIN_LED_TX, false, false);
	// The backlight, off until the UI sets it
	mcu_pin_out(PIN_BACKLIGHT, false, false);
	pwrkey_init(&power_key);
}

// Dropping the hold line cuts the supply on a handheld: there is no standby to fall back to
static void latch(bool on)
{
	if (!on) {
		board_rf_safe();
	}
	mcu_pin_write(PIN_POWER_HOLD, on);
}

static uint16_t battery_mv(void)
{
	uint32_t sum = 0;
	for (uint32_t i = 0; i < BATT_SAMPLES; i++) {
		sum += mcu_adc_read(ADC_CH_BATTERY);
	}
	uint32_t raw = sum / BATT_SAMPLES;
	return (uint16_t)((raw * BATT_MV_NUM) / BATT_MV_DEN);
}

static uint8_t battery_pct(void)
{
	uint32_t mv = battery_mv();
	const uint32_t n = sizeof(curve) / sizeof(curve[0]);
	if (mv <= curve[0].mv) {
		return 0;
	}
	for (uint32_t i = 1; i < n; i++) {
		if (mv < curve[i].mv) {
			uint32_t span = curve[i].mv - curve[i - 1].mv;
			uint32_t rise = curve[i].pct - curve[i - 1].pct;
			return (uint8_t)(curve[i - 1].pct + ((mv - curve[i - 1].mv) * rise) / span);
		}
	}
	return 100;
}

// Placeholder: a linear map over the converter's range. The pot's real travel is unrecorded
static int8_t volume(void)
{
	uint32_t raw = mcu_adc_read(ADC_CH_VOLUME);
	return (int8_t)((raw * 100u) / 4095u);
}

// The stock firmware powers the radio down while PA0 reads high, so a press is high here
static bool switch_on(void)
{
	return pwrkey_on(&power_key, mcu_pin_read(PIN_POWER_KEY));
}

// PE3 is driven from the receive and call paths and PE5 where transmit starts, so they are the green and red
// lights. PE4 is the Bluetooth indicator, driven by that path and not by the core, so LED_BLUE does nothing here
// and the board does not set CAP_LED_RGB (hardware-d878uv2.md 4.3)
static void led(uint8_t which, bool on)
{
	if (which == LED_GREEN) {
		mcu_pin_write(PIN_LED_RX, on);
	} else if (which == LED_RED) {
		mcu_pin_write(PIN_LED_TX, on);
	}
}

// On or off only. The backlight is TIM8 channel 2's complementary output on PB0 and the stock firmware dims it with
// PWM; it also reconfigures PB0 as a plain output driven low to kill it, which is what this does. Dimming waits for
// the timer set-up (hardware-d878uv2.md 4.3)
void d878uv2_backlight(uint8_t percent)
{
	mcu_pin_out(PIN_BACKLIGHT, percent != 0, false);
}

const hal_power_t d878uv2_power = {
	.init = init,
	.latch = latch,
	.battery_mv = battery_mv,
	.battery_pct = battery_pct,
	.volume = volume,
	.switch_on = switch_on,
	.led = led,
};
