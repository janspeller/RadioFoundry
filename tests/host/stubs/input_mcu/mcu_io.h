// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Pin model for input and power-startup integration tests.
#ifndef HOST_INPUT_MCU_IO_H
#define HOST_INPUT_MCU_IO_H
#include <stdbool.h>
#include <stdint.h>
#include "hal_system.h"
extern const hal_system_t mcu_system;
typedef struct { uint16_t OCTL, in; } mcu_gpio_t;
extern mcu_gpio_t host_gpio[5];
#define GPIOA (&host_gpio[0])
#define GPIOB (&host_gpio[1])
#define GPIOC (&host_gpio[2])
#define GPIOD (&host_gpio[3])
#define GPIOE (&host_gpio[4])
#define MCU_PIN(n) (1u << (n))
#define MCU_PULL_UP 1u
#define MCU_PULL_DOWN 2u
#define MCU_EXTI9_5_HANDLER host_encoder_irq
#define MCU_EXTI9_5_IRQn 23
void mcu_pin_in(mcu_gpio_t *port, uint16_t pins, unsigned pull);
bool mcu_pin_read(mcu_gpio_t *port, uint16_t pin);
void mcu_port_put(mcu_gpio_t *port, uint16_t set, uint16_t clear);
void mcu_exti_route(mcu_gpio_t *port, unsigned line);
void mcu_exti_both_edges(uint32_t lines);
void mcu_exti_clear(uint32_t lines);
void mcu_exti_enable(uint32_t lines);
void NVIC_SetPriority(unsigned irq, unsigned priority);
void NVIC_EnableIRQ(unsigned irq);
bool mcu_clock_init(uint32_t crystal, uint32_t core);
void mcu_jtag_release(void);
#endif
