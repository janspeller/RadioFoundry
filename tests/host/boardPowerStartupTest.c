// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The real board startup must initialize its power inputs before any delays or peripherals.
#include <assert.h>
#include <stdio.h>
#include BOARD_STARTUP_SOURCE
mcu_gpio_t host_gpio[5];
static bool initialized;
static void power_init(void) { assert(!initialized); initialized = true; }
const hal_power_t POWER_DEVICE = { .init = power_init };
void hal_delay_ms(uint32_t ms) { (void)ms; assert(initialized); }
bool NAND_INIT(void) { assert(initialized); return true; }
bool CAL_LOAD(void) { assert(initialized); return true; }
bool MODEM_INIT(void) { assert(initialized); return true; }
int main(void)
{
	board_init();
	assert(initialized);
	puts("boardPowerStartupTest PASSED");
	return 0;
}
