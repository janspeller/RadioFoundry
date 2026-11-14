// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Artery AT32F423 registers: the peripherals this platform drives, written from the reference manual (RM V2.04: CRM 4.3,
// FLASH 5.8, GPIO 6.3, USART 12.13, SPI 13.4, WDT 16.5, ADC 18.5, DEBUG 25.4) and the AT32F423 SVD (base addresses).
// Only what the platform uses; the names are the manual's. The Cortex-M4F core is CMSIS's (core_cm4.h).
#ifndef AT32F423_H
#define AT32F423_H

#include <stdint.h>
#include <stddef.h>

// RM 1.1.3, Table 1-3: the device interrupts the platform knows by name
typedef enum {
	NonMaskableInt_IRQn	= -14,
	HardFault_IRQn		= -13,
	MemoryManagement_IRQn	= -12,
	BusFault_IRQn		= -11,
	UsageFault_IRQn		= -10,
	SVCall_IRQn		= -5,
	DebugMonitor_IRQn	= -4,
	PendSV_IRQn		= -2,
	SysTick_IRQn		= -1,
	WWDT_IRQn		= 0,
	FLASH_IRQn		= 4,
	CRM_IRQn		= 5,
	EXINT0_IRQn		= 6,
	DMA1_Channel1_IRQn	= 11,
	ADC1_IRQn		= 18,
	TMR2_IRQn		= 28,
	SPI1_IRQn		= 35,
	SPI2_IRQn		= 36,
	USART1_IRQn		= 37,
	USART2_IRQn		= 38,
	USART3_IRQn		= 39,
	USART6_IRQn		= 71,
} IRQn_Type;

#define AT32_IRQ_COUNT		104u	// external vectors 0..103 (ACC is the last, RM Table 1-3)

#define __CM4_REV		0x0001u
#define __MPU_PRESENT		1
#define __NVIC_PRIO_BITS	4
#define __Vendor_SysTickConfig	0
#define __FPU_PRESENT		1

#include "core_cm4.h"

#define AT32_REG		volatile uint32_t

// ---- CRM, clock and reset (RM 4.3) ----
typedef struct {
	AT32_REG CTRL;		// 0x00
	AT32_REG PLLCFG;	// 0x04
	AT32_REG CFG;		// 0x08
	AT32_REG CLKINT;	// 0x0C
	AT32_REG AHBRST1;	// 0x10
	AT32_REG AHBRST2;
	AT32_REG AHBRST3;
	uint32_t rsv0;
	AT32_REG APB1RST;	// 0x20
	AT32_REG APB2RST;
	uint32_t rsv1[2];
	AT32_REG AHBEN1;	// 0x30
	AT32_REG AHBEN2;
	AT32_REG AHBEN3;
	uint32_t rsv2;
	AT32_REG APB1EN;	// 0x40
	AT32_REG APB2EN;
	uint32_t rsv3[2];
	AT32_REG AHBLPEN1;	// 0x50
	AT32_REG AHBLPEN2;
	AT32_REG AHBLPEN3;
	uint32_t rsv4;
	AT32_REG APB1LPEN;	// 0x60
	AT32_REG APB2LPEN;
	AT32_REG PICLKS;	// 0x68
	uint32_t rsv5;
	AT32_REG BPDC;		// 0x70
	AT32_REG CTRLSTS;	// 0x74
	uint32_t rsv6[10];
	AT32_REG MISC1;		// 0xA0
	AT32_REG MISC2;		// 0xA4
} CRM_Type;
_Static_assert(offsetof(CRM_Type, MISC2) == 0xA4, "CRM layout (RM Table 4-1)");

#define CRM_CTRL_HICKEN		(1u << 0)
#define CRM_CTRL_HICKSTBL	(1u << 1)
#define CRM_CTRL_PLLEN		(1u << 24)
#define CRM_CTRL_PLLSTBL	(1u << 25)
#define CRM_PLLCFG_MS_Pos	0
#define CRM_PLLCFG_NS_Pos	6
#define CRM_PLLCFG_FR_Pos	16
#define CRM_PLLCFG_PLLRCS	(1u << 22)
#define CRM_CFG_SCLKSEL_Msk	(3u << 0)
#define CRM_CFG_SCLKSEL_PLL	(2u << 0)	// PLL/2
#define CRM_CFG_SCLKSTS_Msk	(3u << 2)
#define CRM_CFG_SCLKSTS_PLL	(2u << 2)
#define CRM_CFG_AHBDIV_Msk	(0xFu << 4)
#define CRM_CFG_APB1DIV_Msk	(7u << 10)
#define CRM_CFG_APB2DIV_Msk	(7u << 13)
#define CRM_MISC1_HICKDIV	(1u << 12)	// 1: the HICK's 48 MHz, 0: HICK/6, 8 MHz
#define CRM_MISC1_HICK_TO_SCLK	(1u << 14)	// 0: SCLK on the HICK is a fixed 8 MHz
#define CRM_MISC2_AUTO_STEP_Msk	(3u << 4)
#define CRM_CTRLSTS_LICKEN	(1u << 0)
#define CRM_CTRLSTS_RSTFC	(1u << 24)
#define CRM_CTRLSTS_NRSTF	(1u << 26)
#define CRM_CTRLSTS_PORRSTF	(1u << 27)
#define CRM_CTRLSTS_SWRSTF	(1u << 28)
#define CRM_CTRLSTS_WDTRSTF	(1u << 29)
#define CRM_CTRLSTS_WWDTRSTF	(1u << 30)

// AHBEN1
#define CRM_AHBEN1_GPIOAEN	(1u << 0)
#define CRM_AHBEN1_DMA1EN	(1u << 22)
// APB1EN
#define CRM_APB1EN_TMR2EN	(1u << 0)
#define CRM_APB1EN_SPI2EN	(1u << 14)
#define CRM_APB1EN_USART2EN	(1u << 17)
#define CRM_APB1EN_USART3EN	(1u << 18)
#define CRM_APB1EN_PWCEN	(1u << 28)
#define CRM_APB1EN_DACEN	(1u << 29)
// APB2EN
#define CRM_APB2EN_TMR1EN	(1u << 0)
#define CRM_APB2EN_USART1EN	(1u << 4)
#define CRM_APB2EN_USART6EN	(1u << 5)
#define CRM_APB2EN_ADCEN	(1u << 8)
#define CRM_APB2EN_SPI1EN	(1u << 12)

// ---- FLASH controller (RM 5.8) ----
typedef struct {
	AT32_REG PSR;		// 0x00
	AT32_REG UNLOCK;
	AT32_REG USD_UNLOCK;
	AT32_REG STS;		// 0x0C
	AT32_REG CTRL;		// 0x10
	AT32_REG ADDR;
	uint32_t rsv0;
	AT32_REG USD;		// 0x1C
	AT32_REG EPPS;		// 0x20
} FLASH_Type;

#define FLASH_PSR_WTCYC_Msk	(7u << 0)
#define FLASH_PSR_PFT_EN	(1u << 4)
#define FLASH_PSR_PFT_EN2	(1u << 6)
#define FLASH_PSR_PFT_LAT_DIS	(1u << 8)

// ---- GPIO (RM 6.3) ----
typedef struct {
	AT32_REG CFGR;		// 0x00: 2 bits a pin, 00 input, 01 output, 10 multiplexed, 11 analog
	AT32_REG OMODE;		// 0x04: 1 = open drain
	AT32_REG ODRVR;		// 0x08
	AT32_REG PULL;		// 0x0C: 2 bits a pin, 00 none, 01 up, 10 down
	AT32_REG IDT;		// 0x10
	AT32_REG ODT;		// 0x14
	AT32_REG SCR;		// 0x18: low half sets, high half clears
	AT32_REG WPR;		// 0x1C
	AT32_REG MUXL;		// 0x20
	AT32_REG MUXH;		// 0x24
	AT32_REG CLR;		// 0x28
	AT32_REG TOGR;		// 0x2C
	uint32_t rsv[3];
	AT32_REG HDRV;		// 0x3C
} GPIO_Type;
_Static_assert(offsetof(GPIO_Type, HDRV) == 0x3C, "GPIO layout (RM Table 6-8)");

#define GPIO_MODE_INPUT		0u
#define GPIO_MODE_OUTPUT	1u
#define GPIO_MODE_MUX		2u
#define GPIO_MODE_ANALOG	3u
#define GPIO_PULL_NONE		0u
#define GPIO_PULL_UP		1u
#define GPIO_PULL_DOWN		2u
// Pin masks, in the form tools/boards.py reads from a board's pins.h ("GPIOB, GPIO_PIN_3")
#define GPIO_PIN_0		(1u << 0)
#define GPIO_PIN_1		(1u << 1)
#define GPIO_PIN_2		(1u << 2)
#define GPIO_PIN_3		(1u << 3)
#define GPIO_PIN_4		(1u << 4)
#define GPIO_PIN_5		(1u << 5)
#define GPIO_PIN_6		(1u << 6)
#define GPIO_PIN_7		(1u << 7)
#define GPIO_PIN_8		(1u << 8)
#define GPIO_PIN_9		(1u << 9)
#define GPIO_PIN_10		(1u << 10)
#define GPIO_PIN_11		(1u << 11)
#define GPIO_PIN_12		(1u << 12)
#define GPIO_PIN_13		(1u << 13)
#define GPIO_PIN_14		(1u << 14)
#define GPIO_PIN_15		(1u << 15)

// ---- USART (RM 12.13) ----
typedef struct {
	AT32_REG STS;		// 0x00
	AT32_REG DT;		// 0x04
	AT32_REG BAUDR;		// 0x08
	AT32_REG CTRL1;		// 0x0C
	AT32_REG CTRL2;		// 0x10
	AT32_REG CTRL3;		// 0x14
	AT32_REG GDIV;		// 0x18
	AT32_REG RTOV;		// 0x1C
	AT32_REG IFC;		// 0x20
} USART_Type;

#define USART_STS_PERR		(1u << 0)
#define USART_STS_FERR		(1u << 1)
#define USART_STS_NERR		(1u << 2)
#define USART_STS_ROERR		(1u << 3)
#define USART_STS_RDBF		(1u << 5)
#define USART_STS_TDC		(1u << 6)
#define USART_STS_TDBE		(1u << 7)
#define USART_STS_BFF		(1u << 8)
#define USART_STS_ERRORS	(USART_STS_PERR | USART_STS_FERR | USART_STS_NERR | USART_STS_ROERR)
#define USART_CTRL1_REN		(1u << 2)
#define USART_CTRL1_TEN		(1u << 3)
#define USART_CTRL1_RDBFIEN	(1u << 5)
#define USART_CTRL1_TDBEIEN	(1u << 7)
#define USART_CTRL1_UEN		(1u << 13)

// ---- SPI (RM 13.4) ----
typedef struct {
	AT32_REG CTRL1;		// 0x00
	AT32_REG CTRL2;		// 0x04
	AT32_REG STS;		// 0x08
	AT32_REG DT;		// 0x0C
	AT32_REG CPOLY;
	AT32_REG RCRC;
	AT32_REG TCRC;
	AT32_REG I2SCTRL;
	AT32_REG I2SCLK;
} SPI_Type;

#define SPI_CTRL1_CLKPHA	(1u << 0)
#define SPI_CTRL1_CLKPOL	(1u << 1)
#define SPI_CTRL1_MSTEN		(1u << 2)
#define SPI_CTRL1_MDIV_Pos	3
#define SPI_CTRL1_SPIEN		(1u << 6)
#define SPI_CTRL1_SWCSIL	(1u << 8)
#define SPI_CTRL1_SWCSEN	(1u << 9)
#define SPI_STS_RDBF		(1u << 0)
#define SPI_STS_TDBE		(1u << 1)
#define SPI_STS_BF		(1u << 7)

// ---- WDT, the independent watchdog (RM 16.5) ----
typedef struct {
	AT32_REG CMD;		// 0x00
	AT32_REG DIV;		// 0x04
	AT32_REG RLD;		// 0x08
	AT32_REG STS;		// 0x0C
	AT32_REG WIN;		// 0x10
} WDT_Type;

// ---- ADC (RM 18.5) ----
typedef struct {
	AT32_REG STS;		// 0x00
	AT32_REG CTRL1;		// 0x04
	AT32_REG CTRL2;		// 0x08
	AT32_REG SPT1;		// 0x0C
	AT32_REG SPT2;		// 0x10
	AT32_REG PCDTO[4];	// 0x14
	AT32_REG VMHB;		// 0x24
	AT32_REG VMLB;		// 0x28
	AT32_REG OSQ1;		// 0x2C
	AT32_REG OSQ2;		// 0x30
	AT32_REG OSQ3;		// 0x34
	AT32_REG PSQ;		// 0x38
	AT32_REG PDT[4];	// 0x3C
	AT32_REG ODT;		// 0x4C
	AT32_REG SPT3;		// 0x50
	AT32_REG OSQ4;		// 0x54
	AT32_REG OSQ5;		// 0x58
	AT32_REG OSQ6;		// 0x5C
	uint32_t rsv0[8];
	AT32_REG OVSP;		// 0x80
	uint32_t rsv1[12];
	AT32_REG CALVAL;	// 0xB4
	uint32_t rsv2[6];
	AT32_REG MISC;		// 0xD0
} ADC_Type;
_Static_assert(offsetof(ADC_Type, MISC) == 0xD0, "ADC layout (RM 18.5)");

#define ADC_STS_OCCE		(1u << 1)
#define ADC_STS_RDY		(1u << 6)
#define ADC_CTRL2_ADCEN		(1u << 0)
#define ADC_CTRL2_ADCAL		(1u << 2)
#define ADC_CTRL2_ADCALINIT	(1u << 3)
#define ADC_CTRL2_OCSWTRG	(1u << 30)
#define ADC_CCTRL_ADCDIV_Pos	16

// ---- DEBUG (RM 25.4) ----
typedef struct {
	AT32_REG IDCODE;	// 0x00
	AT32_REG CTRL;		// 0x04
	AT32_REG APB1_PAUSE;	// 0x08
	AT32_REG APB2_PAUSE;	// 0x0C
} DEBUG_Type;

#define DEBUG_APB1_PAUSE_WDT	(1u << 12)	// the WDT stops while a debugger halts the core

// ---- Base addresses (RM 2.4, the SVD) ----
#define AT32_FLASH_BASE		0x08000000u
#define AT32_SRAM_BASE		0x20000000u
#define AT32_SRAM_SIZE		(48u * 1024u)
#define TMR2_BASE		0x40000000u
#define WDT_BASE		0x40003000u
#define SPI2_BASE		0x40003800u
#define USART2_BASE		0x40004400u
#define USART3_BASE		0x40004800u
#define PWC_BASE		0x40007000u
#define DAC_BASE		0x40007400u
#define TMR1_BASE		0x40010000u
#define USART1_BASE		0x40011000u
#define USART6_BASE		0x40011400u
#define ADC1_BASE		0x40012000u
#define ADCCOM_CCTRL_ADDR	0x40012304u
#define SPI1_BASE		0x40013000u
#define GPIOA_BASE		0x40020000u
#define GPIO_PORT_STRIDE	0x400u
#define GPIOB_BASE		0x40020400u
#define GPIOC_BASE		0x40020800u
#define GPIOF_BASE		0x40021400u
#define CRM_BASE		0x40023800u
#define FLASHC_BASE		0x40023C00u
#define DEBUG_BASE		0xE0042000u
#define AT32_F_SIZE_ADDR	0x1FFFF7E0u	// RM 1.3: flash size in KB (16 bits)
#define AT32_UID_ADDR		0x1FFFF7E8u	// RM 1.3: 96-bit unique ID

#define CRM			((CRM_Type *)CRM_BASE)
#define FLASHC			((FLASH_Type *)FLASHC_BASE)
#define GPIOA			((GPIO_Type *)GPIOA_BASE)
#define GPIOB			((GPIO_Type *)GPIOB_BASE)
#define GPIOC			((GPIO_Type *)GPIOC_BASE)
#define GPIOF			((GPIO_Type *)GPIOF_BASE)
#define USART1			((USART_Type *)USART1_BASE)
#define USART3			((USART_Type *)USART3_BASE)
#define USART6			((USART_Type *)USART6_BASE)
#define SPI1			((SPI_Type *)SPI1_BASE)
#define SPI2			((SPI_Type *)SPI2_BASE)
#define WDT			((WDT_Type *)WDT_BASE)
#define ADC1			((ADC_Type *)ADC1_BASE)
#define ADCCOM_CCTRL		(*(AT32_REG *)ADCCOM_CCTRL_ADDR)
#define DEBUGMCU		((DEBUG_Type *)DEBUG_BASE)

#endif
