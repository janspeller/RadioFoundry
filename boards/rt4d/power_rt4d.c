// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D battery on PB2 (ADC1 IN20), status LEDs on PA13 / PA14 (docs/spec/hardware-rt4d.md 4). The radio has no
// power latch or switch the MCU sees (the knob switches the supply), so latch() does nothing and switch_on() is
// always true. The volume knob is in the analog path only (3), so volume() is -1.
#include "rt4d.h"
#include "pins.h"
#include "platform.h"
#include "hal_time.h"
#include "power/battery/battery.h"

// The stock scale (4): raw x 4 / 66 is the voltage in 0.1 V, so mV = raw x 400 / 66. Two-cell pack, inferred
#define BAT_MV_NUM		400u
#define BAT_MV_DEN		66u
#define BAT_FULL_MV		8200u
#define BAT_EMPTY_MV		6400u	// the stock's top warning level
#define BAT_SAMPLES		4u

static battery_avg_t bat;

static void init(void)
{
	at32_pin_analog(PIN_PORT(PIN_BATTERY), PIN_MASK(PIN_BATTERY));
	at32_adc_init();
	bat.valid = false;
#if !defined(RT4D_KEEP_SWD)
	PIN_OUT(PIN_LED_RED, false);
	PIN_OUT(PIN_LED_GREEN, false);
#endif
}

static void latch(bool on)
{
	(void)on;
}

static uint32_t battery_read_mv(void)
{
	uint32_t sum = 0;
	for (unsigned i = 0; i < BAT_SAMPLES; i++) {
		int32_t raw = at32_adc_read(ADC_CH_BATTERY);
		if (raw < 0) {
			return 0;
		}
		sum += (uint32_t)raw;
	}
	return sum * BAT_MV_NUM / (BAT_MV_DEN * BAT_SAMPLES);
}

static uint16_t battery_mv(void)
{
	return battery_avg_mv(&bat, hal_millis(), battery_read_mv);
}

static uint8_t battery_pct(void)
{
	return battery_linear_pct(battery_mv(), BAT_EMPTY_MV, BAT_FULL_MV);
}

static int8_t volume(void)
{
	return -1;
}

static bool switch_on(void)
{
	return true;
}

// Driving PA13 / PA14 ends the debugger's access (2): a bench build with RT4D_KEEP_SWD leaves them alone
static void led(uint8_t which, bool on)
{
#if defined(RT4D_KEEP_SWD)
	(void)which;
	(void)on;
#else
	if (which == LED_RED) {
		PIN_WRITE(PIN_LED_RED, on);
	} else {
		PIN_WRITE(PIN_LED_GREEN, on);
	}
#endif
}

const hal_power_t rt4d_power = {
	.init = init,
	.latch = latch,
	.battery_mv = battery_mv,
	.battery_pct = battery_pct,
	.volume = volume,
	.switch_on = switch_on,
	.led = led,
};
