// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// GPIO and IOMUX on the registers (RM 6.3): mode in CFGR (2 bits a pin), output type in OMODE, pulls in PULL, the
// multiplexed function number in MUXL / MUXH (4 bits a pin). Every read-modify-write runs with interrupts off, so pins
// of one port can be set up from any task.
#include "platform.h"

void at32_gpio_clock(GPIO_Type *port)
{
	uint32_t index = ((uint32_t)(uintptr_t)port - GPIOA_BASE) / GPIO_PORT_STRIDE;
	CRM->AHBEN1 |= CRM_AHBEN1_GPIOAEN << index;
	(void)CRM->AHBEN1;
}

// Every pin of the mask: its 2-bit field in a register set to v
static uint32_t field2(uint32_t reg, uint16_t pins, uint32_t v)
{
	for (uint32_t n = 0; n < 16u; n++) {
		if (pins & (1u << n)) {
			reg = (reg & ~(3u << (2u * n))) | (v << (2u * n));
		}
	}
	return reg;
}

void at32_pin_force(GPIO_Type *port, uint16_t pins, bool level)
{
	at32_gpio_clock(port);
	at32_pin_write(port, pins, level);	// level first, so the pin never glitches
	port->OMODE &= ~(uint32_t)pins;
	port->CFGR = field2(port->CFGR, pins, GPIO_MODE_OUTPUT);
}

static void pin_set(GPIO_Type *port, uint16_t pins, uint32_t mode, bool open_drain, uint8_t pull)
{
	at32_gpio_clock(port);
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if (open_drain) {
		port->OMODE |= pins;
	} else {
		port->OMODE &= ~(uint32_t)pins;
	}
	port->PULL = field2(port->PULL, pins, pull);
	port->CFGR = field2(port->CFGR, pins, mode);
	__set_PRIMASK(primask);
}

void at32_pin_out(GPIO_Type *port, uint16_t pins, bool level, bool open_drain)
{
	at32_gpio_clock(port);
	at32_pin_write(port, pins, level);
	pin_set(port, pins, GPIO_MODE_OUTPUT, open_drain, GPIO_PULL_NONE);
}

void at32_pin_in(GPIO_Type *port, uint16_t pins, uint8_t pull)
{
	pin_set(port, pins, GPIO_MODE_INPUT, false, pull);
}

void at32_pin_mux(GPIO_Type *port, uint16_t pins, uint8_t mux, bool open_drain, uint8_t pull)
{
	at32_gpio_clock(port);
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	for (uint32_t n = 0; n < 16u; n++) {
		if (pins & (1u << n)) {
			volatile uint32_t *r = n < 8u ? &port->MUXL : &port->MUXH;
			uint32_t sh = 4u * (n & 7u);
			*r = (*r & ~(0xFu << sh)) | ((uint32_t)(mux & 0xFu) << sh);
		}
	}
	__set_PRIMASK(primask);
	pin_set(port, pins, GPIO_MODE_MUX, open_drain, pull);
}

void at32_pin_analog(GPIO_Type *port, uint16_t pins)
{
	pin_set(port, pins, GPIO_MODE_ANALOG, false, GPIO_PULL_NONE);
}
