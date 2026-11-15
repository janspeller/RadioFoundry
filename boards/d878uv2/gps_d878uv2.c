// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS GPS (docs/spec/hardware-d878uv2.md 4.1, 4.3): the module on USART2 (PA2 TX, PA3 RX), 9600 8N1,
// through the platform's GPS driver.
//
// Its supply is **not an MCU pin**: the stock firmware drives the RF chip's gpio2 high immediately before it
// enables the receive interrupt and sends the first command, and low when the GPS is turned off and on both
// shutdown paths (4.3). So the enable goes through the RF chip's GPIO register, like the band switching.
//
// The module carries an AT6558, the chip an ATGM336H holds (1.1), and not every radio has one fitted. A CASIC
// module is known by its first $xxTXT sentence and gets the set-up below; any other module's default output is
// parsed as it stands.
#include "config.h"
#include "d878uv2.h"
#include "pins.h"
#include "gps_uart/gps_uart.h"

#if CONFIG_GPS	// off: the driver and its interrupt handler are left out, and the radio has no GPS

// pins.h gives a pin as "port, mask"
#define PORT_OF(p)		PORT_OF_(p)
#define PORT_OF_(port, mask)	port
#define MASK_OF(p)		MASK_OF_(p)
#define MASK_OF_(port, mask)	mask

// The CASIC set-up, sent on the first $xxTXT: one fix a second, GGA and RMC only (what the position source reads).
// The GPS status screen (CONFIG_GPS_SKY) reads GSV and GSA all the time, so they stay on with it
#if CONFIG_GPS_SKY
#define PCAS03	"PCAS03,1,0,1,1,1,0,0,0\n"	// GGA, GSA, GSV, RMC
#else
#define PCAS03	"PCAS03,1,0,0,0,1,0,0,0\n"	// GGA, RMC
#endif
static const gps_setup_t setup[] = {
	{ "$??TXT", "PCAS02,1000\n" PCAS03 "PCAS04,1\n" },
};

static const mcu_gps_cfg_t gps = {
	.uart = MCU_USART2, .irq = MCU_USART2_IRQn,
	.rx_port = PORT_OF(PIN_GPS_RX), .rx_pin = MASK_OF(PIN_GPS_RX),
	.baud = 9600,
	// The module answers about a second after its supply comes on, and that supply waits for the RF chip to be set
	// up: 8 s leaves room for both
	.listen_ms = 8000,
	.power = d878uv2_gps_power,
	.tx_port = PORT_OF(PIN_GPS_TX), .tx_pin = MASK_OF(PIN_GPS_TX),
	.setup_n = sizeof setup / sizeof setup[0], .setup = setup,
};

void MCU_USART2_HANDLER(void)
{
	mcu_gps_irq();
}

void d878uv2_gps_init(void)
{
	mcu_gps_init(&gps);
}
#endif
