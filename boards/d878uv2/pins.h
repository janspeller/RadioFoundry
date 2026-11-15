// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AnyTone AT-D878UVII PLUS pin map (docs/spec/hardware-d878uv2.md 4). Each pin is a port and a bit mask.
//
// Read from the radio's stock firmware, not from a radio: every pin here is unverified until the bench confirms it
// (docs/testing/d878uv2-bringup.md, "Confirming the pin map"). It is the AT-D868UV's map everywhere except the LCD
// read strobe (absent here), the backlight and tuning PWMs, I2C's two lines and the daughter board's UART
// (hardware-d878uv2.md 4.5). It is NOT the family's mobile's map: that board wires the LCD control lines and the
// NAND clock and data differently, so no other board's pins.h may be copied here.
#ifndef D878UV2_PINS_H
#define D878UV2_PINS_H

#include "mcu_io.h"

// LCD, 8080 bus: data on PE7-PE14 (bit 0 on PE7), control on PB10-PB13. This board drives no read strobe, so the
// transport never turns the data bus around for the panel (hardware-d878uv2.md 4.2)
#define LCD_DATA_PORT		GPIOE
#define LCD_DATA_SHIFT		7u
#define LCD_DATA_MASK		((uint16_t)(0xFFu << LCD_DATA_SHIFT))
#define PIN_LCD_RS		GPIOB, MCU_PIN(10)	// D/C: low = command
#define PIN_LCD_RESET		GPIOB, MCU_PIN(11)
#define PIN_LCD_CS		GPIOB, MCU_PIN(12)
#define PIN_LCD_WR		GPIOB, MCU_PIN(13)

// Keypad: the LCD data lines drive the matrix, one low at a time; the sense lines are pull-up inputs, keys active
// low. The scan has to leave the data bus as the LCD left it (hardware-d878uv2.md 4.1)
#define KEY_DRIVE_PORT		LCD_DATA_PORT
#define KEY_DRIVE_SHIFT		LCD_DATA_SHIFT
// Only three of the inputs are matrix senses (hardware-d878uv2.md 4.3). The 868 page lists six, which is wrong
// here and probably there too
#define PIN_KEY_SENSE0		GPIOB, MCU_PIN(1)
#define PIN_KEY_SENSE1		GPIOB, MCU_PIN(2)
#define PIN_KEY_SENSE2		GPIOC, MCU_PIN(5)

// The direct keys, wired to ground and read with every drive line high (hardware-d878uv2.md 4.3). PF3 hangs off
// PB2, which is also matrix column 1: while it is held that column cannot be scanned, which is exactly the case
// drivers/input/keymatrix models with its `direct` array
#define PIN_PTT			GPIOA, MCU_PIN(10)
#define PIN_PF1			GPIOE, MCU_PIN(1)
#define PIN_PF2			GPIOA, MCU_PIN(14)
// PA13 is NOT a key: it is read with its output register cleared, so the internal pull-down is selected and it
// reads high only when something drives it. The stock firmware gates the key task and picks an audio scaling from
// it, which is the shape of an accessory or headset detect on the side connector (4.3, not identified)
#define PIN_ACCESSORY_DETECT	GPIOA, MCU_PIN(13)

// Channel knob: quadrature on pull-up inputs, EXTI on PC8 (both edges)
#define PIN_KNOB_A		GPIOC, MCU_PIN(8)
#define PIN_KNOB_B		GPIOC, MCU_PIN(9)

// SPI NAND, bit-banged. IO0 and IO1 both carry data on a dual-output read, so PB15 turns around
#define PIN_NAND_CS		GPIOB, MCU_PIN(8)
#define PIN_NAND_CLK		GPIOB, MCU_PIN(14)
#define PIN_NAND_MOSI		GPIOB, MCU_PIN(15)	// IO0
#define PIN_NAND_MISO		GPIOA, MCU_PIN(15)	// IO1

// AT1846S 3-wire bus: 8-bit address (bit 7 set = read) then 16 bits MSB first. SDIO turns around for a read
#define PIN_RF_SCLK		GPIOB, MCU_PIN(3)
#define PIN_RF_SDIO		GPIOB, MCU_PIN(4)
#define PIN_RF_CS		GPIOB, MCU_PIN(5)	// SEN, low = selected

// SCT3258 host port: data PD8-PD15, strobes and handshake on PD2-PD5. Data is latched on the rising edge of WR
#define HPI_DATA_PORT		GPIOD
#define HPI_DATA_SHIFT		8u
#define PIN_HPI_CS		GPIOD, MCU_PIN(2)	// HCSN
#define PIN_HPI_WR		GPIOD, MCU_PIN(3)	// HWRN
#define PIN_HPI_RD		GPIOD, MCU_PIN(4)	// HRDN
#define PIN_HPI_HOBI		GPIOD, MCU_PIN(5)	// input: low = data waiting

// SCT3258 control. PD1, PD7 and PC7 are driven through the same reset and power-down sequences; what they gate is
// not identified, so the board drives them exactly as the sequence in hardware-d878uv2.md 4.1 says
#define PIN_SCT_RESET		GPIOD, MCU_PIN(6)	// RSTN
#define PIN_SCT_STRAP		GPIOC, MCU_PIN(12)	// PIO1: low = Intel timing, then released to a pulled-down input
#define PIN_SCT_EN0		GPIOA, MCU_PIN(8)
#define PIN_SCT_EN1		GPIOC, MCU_PIN(6)
#define PIN_SCT_AUX0		GPIOD, MCU_PIN(1)
#define PIN_SCT_AUX1		GPIOD, MCU_PIN(7)
#define PIN_SCT_AUX2		GPIOC, MCU_PIN(7)
// The baseband's slot-timing output (PIO3): EXTI0, both edges, the highest priority on the board. High keys the PA
#define PIN_SCT_SLOT		GPIOD, MCU_PIN(0)

// I2C, bit-banged, 7-bit addressing. SDA is the line that turns around. Devices: 0x10 broadcast FM, 0x1D
// accelerometer. The audio codec is NOT here: the baseband sets it up (hardware-anytone.md 2.5)
#define PIN_I2C_SDA		GPIOB, MCU_PIN(6)
#define PIN_I2C_SCL		GPIOB, MCU_PIN(7)

// Transmit: the PA gate is two lines driven together, then the power ramp on the DAC
#define PIN_PA_EN0		GPIOE, MCU_PIN(6)
#define PIN_PA_EN1		GPIOC, MCU_PIN(4)
#define PIN_TX_POWER_DAC	0u			// DAC channel 1 on PA4, top 8 bits only
#define PIN_CONTROL_DAC		1u			// DAC channel 2 on PA5, full 12-bit calibration count
#define PIN_TUNE_PWM_PORT	GPIOA			// PA6, TIM3 channel 1: frequency-indexed control voltage
#define PIN_TUNE_PWM_BIT	MCU_PIN(6)

// Audio and indicators
#define PIN_SPK_EN		GPIOA, MCU_PIN(7)	// speaker amplifier enable, active high
#define PIN_AUDIO_SW		GPIOE, MCU_PIN(0)	// driven opposite PIN_SPK_EN on receive; not identified
#define PIN_TONE_PWM		GPIOB, MCU_PIN(9)	// TIM4 channel 4 while sounding, analog when idle
#define PIN_BACKLIGHT		GPIOB, MCU_PIN(0)	// TIM8 channel 2 complementary output (CH2N)
#define PIN_LED_RX		GPIOE, MCU_PIN(3)	// green: the receive and call paths
#define PIN_LED_BT		GPIOE, MCU_PIN(4)	// most likely the Bluetooth indicator, driven by that path
#define PIN_LED_TX		GPIOE, MCU_PIN(5)	// red: transmit, and the battery monitor

// Power
#define PIN_POWER_KEY		GPIOA, MCU_PIN(0)	// pull-up input, 10 ms debounce; high while held
#define PIN_POWER_HOLD		GPIOC, MCU_PIN(3)	// high keeps the radio on
#define ADC_CH_BATTERY		11u			// PC1, about a 1:4 divider; 600 and 610 are 6.00 V and 6.10 V
#define ADC_CH_VOLUME		12u			// PC2, a panel potentiometer (unidentified)
#define ADC_CH_UNKNOWN		10u			// PC0, sampled into a ring buffer; what it measures is open

// Serial. USART1, USART3 and UART5 are unused on this board
#define PIN_GPS_TX		GPIOA, MCU_PIN(2)	// USART2, 9600 baud, NMEA
#define PIN_GPS_RX		GPIOA, MCU_PIN(3)
#define PIN_BT_TX		GPIOC, MCU_PIN(10)	// UART4, 115200, the Bluetooth and APRS daughter board
#define PIN_BT_RX		GPIOC, MCU_PIN(11)
#define PIN_BT_RESET		GPIOA, MCU_PIN(1)	// low 5 ms restarts the daughter board
#define PIN_BT_STATUS		GPIOE, MCU_PIN(15)	// driven by the daughter board's state; the 868 has its LCD RD here

// USB: the device pins, plus the attach switch the stack drives
#define PIN_USB_DM		GPIOA, MCU_PIN(11)
#define PIN_USB_DP		GPIOA, MCU_PIN(12)
#define PIN_USB_ATTACH		GPIOE, MCU_PIN(2)

#endif
