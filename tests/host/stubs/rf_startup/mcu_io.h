// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// GPIO and DAC stand-ins for board glue tests; no hardware addresses are accessed.
#ifndef HOST_MCU_IO_H
#define HOST_MCU_IO_H
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
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
extern unsigned host_af_mask[5];
static inline void mcu_pin_af(unsigned port, unsigned pins, unsigned af, bool od)
{
	(void)af; (void)od; host_af_mask[port] |= pins;
}
extern uint32_t host_timer_regs[6][16];
extern unsigned host_timer_clocks;
#define TIMER_BASE(n) (0x40000000u + 0x400u * ((n) - 1u))
#define MCU_TIM(n) (0x40000000u + 0x400u * ((n) - 2u))
static inline void mcu_tim_clock_on(unsigned n) { assert(n >= 2 && n <= 7); host_timer_clocks |= 1u << (n - 2u); }
static inline bool mcu_tim_clocked(unsigned n) { return (host_timer_clocks & (1u << (n - 2u))) != 0; }
static inline uint32_t *host_timer_reg(uint32_t base, unsigned off)
{
	assert(base >= 0x40000000u && base < 0x40001800u && (base & 0x3FFu) == 0);
	unsigned i = (base - 0x40000000u) / 0x400u;
	assert(host_timer_clocks & (1u << i)); // writes to an unclocked timer are not a functioning PWM
	assert(off < 64 && (off & 3u) == 0);
	return &host_timer_regs[i][off / 4u];
}
#define TIMER_CTL0(b) (*host_timer_reg((b), 0x00))
#define TIMER_CHCTL0(b) (*host_timer_reg((b), 0x18))
#define TIMER_CHCTL2(b) (*host_timer_reg((b), 0x20))
#define TIMER_PSC(b) (*host_timer_reg((b), 0x28))
#define TIMER_CAR(b) (*host_timer_reg((b), 0x2C))
#define TIMER_CH0CV(b) (*host_timer_reg((b), 0x34))
#define TIMER_CTL0_CEN 1u
#define TIMER_CHCTL0_CH0_PWM1 (6u << 4)
#define TIMER_CHCTL0_CH0_PRELOAD (1u << 3)
#define TIMER_CHCTL2_CH0EN 1u
#define MCU_EXTI0_IRQn 6u
static inline void mcu_exti_clear(unsigned lines) { (void)lines; }
static inline void mcu_exti_route(unsigned port, unsigned line) { (void)port; (void)line; }
static inline void mcu_exti_both_edges(unsigned lines) { (void)lines; }
static inline void mcu_exti_enable(unsigned lines) { (void)lines; }
static inline void NVIC_SetPriority(unsigned irq, unsigned priority) { (void)irq; (void)priority; }
static inline void NVIC_EnableIRQ(unsigned irq) { (void)irq; }
extern unsigned host_dac_init_mask, host_dac_writes[2];
extern uint16_t host_dac_values[2];
static inline void mcu_dac_init(unsigned channel) { host_dac_init_mask |= 1u << channel; }
static inline void mcu_dac_write(unsigned channel, uint16_t value)
{
	host_dac_values[channel] = value;
	host_dac_writes[channel]++;
}
#endif
