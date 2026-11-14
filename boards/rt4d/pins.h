// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Radtel RT-4D pin map (docs/spec/hardware-rt4d.md 2). From the schematic and the public pin table, which agree; no pin
// here has been checked on a radio running RadioFoundry yet.
#ifndef RT4D_PINS_H
#define RT4D_PINS_H

#include "platform.h"

// Broadcast FM receiver (BK1080, no driver yet)
#define PIN_BCAST_SCK		GPIOC, GPIO_PIN_13
#define PIN_BCAST_SDA		GPIOC, GPIO_PIN_14
#define PIN_BCAST_POWER		GPIOC, GPIO_PIN_15

// Key matrix: KEYOUT1-4 driven (columns), KEYIN1-4 read (rows), section 4
#define PIN_KEYOUT1		GPIOA, GPIO_PIN_0
#define PIN_KEYOUT2		GPIOA, GPIO_PIN_1
#define PIN_KEYOUT3		GPIOF, GPIO_PIN_0
#define PIN_KEYOUT4		GPIOB, GPIO_PIN_0
#define PIN_KEYIN1		GPIOF, GPIO_PIN_1	// also the A/B key to ground
#define PIN_KEYIN2		GPIOA, GPIO_PIN_8	// also a side key to ground
#define PIN_KEYIN3		GPIOA, GPIO_PIN_9	// also a side key to ground
#define PIN_KEYIN4		GPIOB, GPIO_PIN_11

// Audio
#define PIN_AMP_EN		GPIOA, GPIO_PIN_2	// LN4898 speaker amplifier enable, high = on
#define PIN_BACKLIGHT		GPIOA, GPIO_PIN_3	// LCD and keypad backlight (Q501), high = on
#define PIN_TONE_DAC		GPIOA, GPIO_PIN_4	// DAC1: tones into the receive audio path (not driven yet)
#define PIN_TX_DTMF_DAC		GPIOA, GPIO_PIN_5	// DAC2: tones into the microphone path (not driven yet)
#define PIN_AF_SW		GPIOF, GPIO_PIN_8	// receive audio: low in FM receive and in DMR, high = FM receive muted (3, 5.5)
#define PIN_MOD_SW		GPIOB, GPIO_PIN_10	// modulation into the BK4819: low = the MCU side (FM), high = the HF6853

// HF6853 DMR chip (no driver yet): power stays off on FM builds (section 5.5)
#define PIN_DIG_RXD		GPIOA, GPIO_PIN_6	// USART3 RX, IOMUX 8
#define PIN_DIG_TXD		GPIOA, GPIO_PIN_7	// USART3 TX, IOMUX 7
#define PIN_DIG_POWER		GPIOB, GPIO_PIN_1	// HF6853 CEN, high = on

// Battery divider, ADC1 IN20
#define PIN_BATTERY		GPIOB, GPIO_PIN_2
#define ADC_CH_BATTERY		20

// Data flash, SPI2 (IOMUX 5) with a GPIO chip select
#define PIN_FLASH_CS		GPIOB, GPIO_PIN_12
#define PIN_FLASH_SCK		GPIOB, GPIO_PIN_13
#define PIN_FLASH_MISO		GPIOB, GPIO_PIN_14
#define PIN_FLASH_MOSI		GPIOB, GPIO_PIN_15
#define SPI_FLASH_MUX		5

// PC link: USART6 (IOMUX 8). RX is also the PTT (to ground), section 4
#define PIN_HOST_TXD		GPIOA, GPIO_PIN_11
#define PIN_HOST_RXD		GPIOA, GPIO_PIN_12
#define HOST_UART_MUX		8

// RF control
#define PIN_UV_APC		GPIOA, GPIO_PIN_10	// the BK4819's APC output to the VHF PA (high) or the UHF PA (low)
#define PIN_DCS_DET		GPIOB, GPIO_PIN_6	// sub-audio detect from the RF sheet (unused)

// LEDs on the SWD pins: driving them ends SWD (RT4D_KEEP_SWD keeps it)
#define PIN_LED_RED		GPIOA, GPIO_PIN_13	// SWDIO
#define PIN_LED_GREEN		GPIOA, GPIO_PIN_14	// SWCLK

// Display: bit-banged serial bus (section 4)
#define PIN_LCD_RST		GPIOF, GPIO_PIN_6
#define PIN_LCD_CS		GPIOA, GPIO_PIN_15
#define PIN_LCD_SCK		GPIOB, GPIO_PIN_3
#define PIN_LCD_CD		GPIOB, GPIO_PIN_4
#define PIN_LCD_SDA		GPIOB, GPIO_PIN_5

// BK4819 3-wire bus, shared with the HF6853 (section 3, 5.5): outputs while the MCU owns it, inputs otherwise
#define PIN_RF_DATA		GPIOB, GPIO_PIN_7
#define PIN_RF_CS		GPIOB, GPIO_PIN_8
#define PIN_RF_CLK		GPIOB, GPIO_PIN_9

// Helpers that take the two-argument pin macros above: PIN_WRITE(PIN_LED_RED, true)
#define PIN_PORT(...)		PIN_PORT2_(__VA_ARGS__)
#define PIN_MASK(...)		PIN_MASK2_(__VA_ARGS__)
#define PIN_PORT2_(port, pin)	(port)
#define PIN_MASK2_(port, pin)	((uint16_t)(pin))
#define PIN_WRITE(p, level)	at32_pin_write(PIN_PORT(p), PIN_MASK(p), (level))
#define PIN_READ(p)		at32_pin_read(PIN_PORT(p), PIN_MASK(p))
#define PIN_OUT(p, level)	at32_pin_out(PIN_PORT(p), PIN_MASK(p), (level), false)
#define PIN_IN(p, pull)		at32_pin_in(PIN_PORT(p), PIN_MASK(p), (pull))
#define PIN_FORCE(p, level)	at32_pin_force(PIN_PORT(p), PIN_MASK(p), (level))

#endif
