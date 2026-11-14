// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The AT32F423 watchdog timer, WDT (hal_watchdog.h, CONFIG_WATCHDOG; RM 16). It runs from the LICK, which the WDT forces
// on; once started only a reset stops it, so hal_watchdog_stop() can't. Divider and reload sit behind the 0x5555 unlock;
// 0xAAAA reloads, 0xCCCC starts (RM 16.5.1).
#include "config.h"

#if CONFIG_WATCHDOG
#include "platform.h"
#include "hal_watchdog.h"

#define CMD_FEED	0xAAAAu
#define CMD_UNLOCK	0x5555u
#define CMD_START	0xCCCCu
#define LICK_HZ		40000u		// typical (RM Table 16-1); the manual gives 30 to 60 kHz
#define DIV_64		4u		// LICK / 64: 1.6 ms a count at 40 kHz
#define RLD_MAX		0xFFFu
// 5 s at the typical LICK, 3.3 s at 60 kHz, 6.7 s at 30 kHz
#define TIMEOUT_MS	5000u
#define RELOAD		(LICK_HZ / 64u * TIMEOUT_MS / 1000u)
_Static_assert(RELOAD - 1u <= RLD_MAX, "the reload fits 12 bits");
#define STS_SPINS	100000u		// DIVF / RLDF clear once the values reach the LICK domain

static bool running;

void hal_watchdog_start(void)
{
	DEBUGMCU->APB1_PAUSE |= DEBUG_APB1_PAUSE_WDT;	// stopped while a debugger halts the core
	WDT->CMD = CMD_START;
	WDT->CMD = CMD_UNLOCK;
	WDT->DIV = DIV_64;
	WDT->RLD = RELOAD - 1u;
	for (uint32_t i = 0; i < STS_SPINS && WDT->STS != 0; i++) {
	}
	WDT->CMD = CMD_FEED;	// reload with the new values, and locked again
	running = true;
}

void hal_watchdog_feed(void)
{
	if (running) {
		WDT->CMD = CMD_FEED;
	}
}

bool hal_watchdog_stop(void)
{
	return false;
}

uint32_t hal_watchdog_timeout_ms(void)
{
	return running ? TIMEOUT_MS : 0;
}

uint8_t hal_watchdog_regs(uint32_t *out)
{
	out[0] = WDT->DIV;
	out[1] = WDT->RLD;
	out[2] = WDT->STS;
	return 3;
}

bool hal_watchdog_test_load(const uint32_t arg[3])
{
	(void)arg;
	return false;
}

#endif	// CONFIG_WATCHDOG
