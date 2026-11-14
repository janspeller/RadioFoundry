// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT32F423 entry: the board's early init with the clocks, then the core application under FreeRTOS
#include "config.h"
#include "platform.h"
#include "board.h"
#include "hal_watchdog.h"
#include "FreeRTOS.h"
#include "task.h"

void at32_time_init(void);
void app_start(void);	// core/app: creates the tasks

int main(void)
{
	board_rf_safe();	// transmitter off before anything that could hang (docs/spec/crash-log.md 8)
	AT32_PROBE(2);
#if CONFIG_WATCHDOG
	hal_watchdog_start();
#endif
	AT32_PROBE(3);
	NVIC_SetPriorityGrouping(0);	// all 4 priority bits preempt (FreeRTOS needs this)
	at32_time_init();
	AT32_PROBE(4);
	board_early_init();	// pins, then at32_clock_init(); its busy waits feed (hal_delay_ms before the scheduler)
	at32_time_init();	// the clock changed; restart the count from the new rate
	AT32_PROBE(5);
#if CONFIG_WATCHDOG
	hal_watchdog_feed();
#endif
	app_start();
#if CONFIG_WATCHDOG
	hal_watchdog_feed();
#endif
	AT32_PROBE(6);
	vTaskStartScheduler();
	for (;;) {
	}
}

// A crash stops the radio here with the transmitter off: the crash log isn't written for this platform yet
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
	(void)task;
	(void)name;
	__disable_irq();
	board_rf_safe();
#if defined(AT32_BOOT_PROBE)
	at32_probe_fault(500);	// slow blinking: an RTOS hook
#endif
	for (;;) {
	}
}

void vApplicationMallocFailedHook(void)
{
	__disable_irq();
	board_rf_safe();
#if defined(AT32_BOOT_PROBE)
	at32_probe_fault(500);	// slow blinking: an RTOS hook
#endif
	for (;;) {
	}
}

// Every fault and every interrupt without a handler (startup.c's weak aliases). With the watchdog running it resets
// the radio a few seconds later
void Default_Handler(void)
{
	__disable_irq();
	board_rf_safe();
#if defined(AT32_BOOT_PROBE)
	at32_probe_fault(100);	// fast blinking: a fault or a stray interrupt
#endif
	for (;;) {
	}
}
