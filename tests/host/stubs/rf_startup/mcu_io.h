// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// GPIO and DAC stand-ins for board glue tests; no hardware addresses are accessed.
#ifndef HOST_MCU_IO_H
#define HOST_MCU_IO_H
#include <stdbool.h>
#include <stdint.h>
#define GPIOA 0u
#define GPIOB 1u
#define GPIOC 2u
#define GPIOD 3u
#define GPIOE 4u
#define MCU_PIN(n) (1u << (n))
#define MCU_PULL_UP 1u
static inline void mcu_pin_write(unsigned port, unsigned pin, bool level) { (void)port; (void)pin; (void)level; }
static inline void mcu_pin_out(unsigned port, unsigned pin, bool level, bool od) { (void)od; mcu_pin_write(port, pin, level); }
static inline void mcu_pin_in(unsigned port, unsigned pin, unsigned pull) { (void)port; (void)pin; (void)pull; }
static inline bool mcu_pin_read(unsigned port, unsigned pin) { (void)port; (void)pin; return false; }
static inline void mcu_pin_analog(unsigned port, unsigned pins) { (void)port; (void)pins; }
extern unsigned host_dac_init_mask, host_dac_writes[2];
extern uint16_t host_dac_values[2];
static inline void mcu_dac_init(unsigned channel) { host_dac_init_mask |= 1u << channel; }
static inline void mcu_dac_write(unsigned channel, uint16_t value)
{
	host_dac_values[channel] = value;
	host_dac_writes[channel]++;
}
#endif
