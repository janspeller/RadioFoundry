// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT32F423 platform services for the boards built on it: clocks, pins, SPI, the host UART, the ADC, the system
// descriptor. Registers come from the reference manual (at32f423.h)
#ifndef PLATFORM_AT32F423_H
#define PLATFORM_AT32F423_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "at32f423.h"
#include "hal_system.h"
#include "hal_stream.h"

extern uint32_t SystemCoreClock;	// HCLK in Hz: the HICK's 8 MHz until at32_clock_init()

// The system clock (clock.c): 72 MHz from the HICK through the PLL, AHB and APB2 undivided, APB1 undivided too (72 MHz is
// below its 120 MHz limit). Returns false and stays on the HICK when the PLL never locks
bool at32_clock_init(void);
// PCLK1 / PCLK2 in Hz, for the baud rate and SPI dividers
uint32_t at32_pclk1_hz(void);
uint32_t at32_pclk2_hz(void);

// Pins: a port and a mask of pins on it (gpio.c). Each one turns its port's clock on
void at32_gpio_clock(GPIO_Type *port);
// For board_rf_safe: the clock on, the level, then push-pull output with plain register writes. Safe in a fault handler
// and before the clocks are set up
void at32_pin_force(GPIO_Type *port, uint16_t pins, bool level);
void at32_pin_out(GPIO_Type *port, uint16_t pins, bool level, bool open_drain);
void at32_pin_in(GPIO_Type *port, uint16_t pins, uint8_t pull);		// GPIO_PULL_*
void at32_pin_mux(GPIO_Type *port, uint16_t pins, uint8_t mux, bool open_drain, uint8_t pull);	// IOMUX function 0..15
void at32_pin_analog(GPIO_Type *port, uint16_t pins);

static inline void at32_pin_write(GPIO_Type *port, uint16_t pins, bool level)
{
	port->SCR = level ? pins : ((uint32_t)pins << 16);
}

static inline bool at32_pin_read(GPIO_Type *port, uint16_t pin)
{
	return (port->IDT & pin) != 0;
}

// SPI master on the registers (spi.c): mode from cpol / cpha, clock PCLK / 2^(mdiv + 1) (mdiv 0..7), software chip
// select (the board drives its pin). Byte at a time, polled: tx NULL sends 0xFF, rx NULL discards. False on a stuck
// peripheral
void at32_spi_master_init(SPI_Type *spi, bool cpol, bool cpha, uint8_t mdiv);
bool at32_spi_xfer(SPI_Type *spi, const uint8_t *tx, uint8_t *rx, uint32_t len);

// The host port on a USART (uart.c): 8N1 at the baud rate given, receive by interrupt into a ring, transmit polled with
// a tick's sleep when the data register stays full. The board names the USART and has set its pins up (at32_pin_mux)
typedef struct {
	USART_Type *usart;
	IRQn_Type irq;
	uint32_t baud;
} at32_host_uart_cfg_t;
void at32_host_uart_configure(const at32_host_uart_cfg_t *cfg);
extern const hal_stream_t at32_host_uart_stream;
uint32_t at32_host_uart_line_errors(void);	// characters the USART flagged (overrun, noise, framing, parity), a count
bool at32_host_uart_last_rx(uint32_t *ms);	// a good byte has arrived, and when (hal_millis)
uint32_t at32_host_uart_overruns(void);		// bytes lost because the ring was full
// The interrupt handler body, for the board's USARTx_IRQHandler of the host USART
void at32_host_uart_isr(void);
// A second USART for a chip on the board: 8N1, receive into the caller's ring (rx_size a power of two), transmit
// by interrupt straight from the caller's frame, each byte cleared once sent; the frame stays the UART's until
// at32_aux_uart_tx_busy() is false. Never waits
typedef struct {
	USART_Type *usart;
	IRQn_Type irq;
	uint32_t baud;
	uint8_t *rx;
	uint32_t rx_size;
} at32_aux_uart_cfg_t;
void at32_aux_uart_start(const at32_aux_uart_cfg_t *cfg);
size_t at32_aux_uart_read(uint8_t *buf, size_t max);
bool at32_aux_uart_write(uint8_t *buf, size_t len);	// false while the previous frame is going out
bool at32_aux_uart_tx_busy(void);
uint32_t at32_aux_uart_errors(void);		// flagged and lost characters

// ADC1, one software-triggered conversion at a time (adc.c). channel: ADC_INx (0..17, 20..27). 12-bit result, or -1 if
// the converter didn't finish
void at32_adc_init(void);
int32_t at32_adc_read(uint8_t channel);

// The system descriptor for board.system (system.c): chip identity and reboot
extern const hal_system_t at32_system;
void at32_reboot(void) __attribute__((noreturn));

// Boot-stage probe (boot_probe.c), a bring-up build only: AT32_BOOT_PROBE with AT32_PROBE_PORT (0 = GPIOA, 1 = GPIOB,
// ...) and AT32_PROBE_PIN, a pin whose level someone can see (a backlight). The pin goes high at reset entry; after
// stage n it goes low n times (AT32_PROBE(n)), twice over, then stays high. A fault blinks it fast forever, an RTOS
// hook (stack overflow, heap exhausted) slowly. Without AT32_BOOT_PROBE every hook compiles to nothing
#if defined(AT32_BOOT_PROBE)
void at32_probe_init(void);
void at32_probe_stage(uint8_t n, bool long_pulses);
void at32_probe_fault(uint32_t half_period_ms) __attribute__((noreturn));
#define AT32_PROBE(n)		at32_probe_stage((n), false)
#else
#define AT32_PROBE(n)		((void)0)
#endif

#endif
