// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Boot-stage probe (platform.h AT32_BOOT_PROBE): progress shown on one pin someone can watch, for a radio that shows
// nothing else yet. Register writes and the DWT cycle counter only, so it works from the reset handler on, before the
// clocks, the RTOS or any driver, and inside a fault handler. The pin's level: high while running, a low pulse per
// count. Delays follow SystemCoreClock (8 MHz until at32_clock_init): with a faster clock left by the bootloader the
// early stages blink faster, never slower. The watchdog is fed in every wait, so one the bootloader may have started
// doesn't reset the radio in the middle of a pattern
#include "platform.h"

#if defined(AT32_BOOT_PROBE)

#ifndef AT32_PROBE_PORT
#error "AT32_BOOT_PROBE needs AT32_PROBE_PORT (0 = GPIOA) and AT32_PROBE_PIN"
#endif

#define PROBE_GPIO	((GPIO_Type *)(GPIOA_BASE + (uint32_t)(AT32_PROBE_PORT) * GPIO_PORT_STRIDE))
#define PROBE_MASK	(1u << (AT32_PROBE_PIN))
#define PULSE_MS	250u	// a count: low this long, then high this long
#define LONG_MS		1000u	// a long count
#define PAUSE_MS	1500u	// between the two showings, and after the second
#define REPEATS		2u

static void level(bool high)
{
	PROBE_GPIO->SCR = high ? PROBE_MASK : PROBE_MASK << 16;
}

static void wait_ms(uint32_t ms)
{
	uint32_t hz = SystemCoreClock;
	if (hz < 1000000u || hz > 200000000u) {
		hz = 8000000u;	// before .data holds it
	}
	uint32_t per_ms = hz / 1000u;
	for (uint32_t i = 0; i < ms; i++) {
		uint32_t start = DWT->CYCCNT;
		while ((uint32_t)(DWT->CYCCNT - start) < per_ms) {
		}
		WDT->CMD = 0xAAAAu;	// reload, if it runs (RM 16.5.1)
	}
}

void at32_probe_init(void)
{
	CRM->AHBEN1 |= CRM_AHBEN1_GPIOAEN << (AT32_PROBE_PORT);
	(void)CRM->AHBEN1;
	level(true);
	PROBE_GPIO->OMODE &= ~PROBE_MASK;
	PROBE_GPIO->CFGR = (PROBE_GPIO->CFGR & ~(3u << (2u * (AT32_PROBE_PIN)))) |
			   (GPIO_MODE_OUTPUT << (2u * (AT32_PROBE_PIN)));
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void at32_probe_stage(uint8_t n, bool long_pulses)
{
	for (uint32_t r = 0; r < REPEATS; r++) {
		for (uint8_t i = 0; i < n; i++) {
			level(false);
			wait_ms(long_pulses ? LONG_MS : PULSE_MS);
			level(true);
			wait_ms(PULSE_MS);
		}
		wait_ms(PAUSE_MS);
	}
}

void at32_probe_fault(uint32_t half_period_ms)
{
	for (;;) {
		level(false);
		wait_ms(half_period_ms);
		level(true);
		wait_ms(half_period_ms);
	}
}

#endif
