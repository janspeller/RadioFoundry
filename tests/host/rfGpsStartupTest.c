// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Run the real RF initialization, scheduling GPS power immediately when its task is created.
#include <assert.h>
#include <stdio.h>
#include RF_STARTUP_SOURCE

static bool gps_started;
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
int main(void)
{
	assert(!gps_started);
	init();
	assert(gps_started);
	puts("rfGpsStartupTest PASSED");
	return 0;
}
