// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The AT32F423's system clock from its internal oscillator (RM 4.1, 4.3). No board on this platform has a crystal
// (hardware-rt4d.md 1), so the PLL runs from the HICK. The set-up is a conservative one inside every limit the manual
// gives, not the fastest:
//
//   HICK 48 MHz, HICKDIV = 0: HICK/6 = 8 MHz into the PLL (RM 4.3.23; the clock tree takes the PLL's HICK input after
//   the divider). PLL_MS = 1: 8 MHz after the pre-divider (2-16 MHz). PLL_NS = 72: VCO 576 MHz (500-1000 MHz).
//   PLL_FR = 4 (code 2): PLLCLK 144 MHz (the datasheet's 32-300 MHz). SCLKSEL = PLL/2: SCLK = HCLK = 72 MHz.
//   APB1 and APB2 undivided, 72 MHz (APB1's limit is 120 MHz). Flash: 2 wait states (64-96 MHz, RM 5.8.1), both
//   prefetch buffers on. LDO stays at its reset 1.2 V, enough for 120 MHz (datasheet, operating conditions).
//
// 72 MHz is what the STM32 radios run at too, and the stock application's own clock is lower still (hardware-rt4d.md 1,
// Clocks): plenty for FM, a 1 bpp UI and bit-banged buses, at a lower current than 150 MHz. The switch uses the
// manual's auto step-by-step mode (RM 4.1.5)
#include "platform.h"

#define HICK_HZ		8000000u	// SCLK after reset: HICK/6
#define PLL_IN_HZ	8000000u
#define PLL_MS		1u
#define PLL_NS		72u
#define PLL_FR_CODE	2u		// post-divider 4
#define PLL_FR		4u
#define SCLK_HZ		(PLL_IN_HZ / PLL_MS * PLL_NS / PLL_FR / 2u)
#define FLASH_WTCYC	2u
#define SPINS		200000u

_Static_assert(PLL_IN_HZ / PLL_MS >= 2000000u && PLL_IN_HZ / PLL_MS <= 16000000u, "PLL input range (RM 4.1.1)");
_Static_assert(PLL_IN_HZ / PLL_MS * PLL_NS >= 500000000u && PLL_IN_HZ / PLL_MS * PLL_NS <= 1000000000u, "VCO range");
_Static_assert(SCLK_HZ == 72000000u, "the clock the comment above describes");

uint32_t SystemCoreClock = HICK_HZ;
static uint32_t pclk1 = HICK_HZ, pclk2 = HICK_HZ;

static bool wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
	for (uint32_t i = 0; i < SPINS; i++) {
		if ((*reg & mask) == want) {
			return true;
		}
	}
	return false;
}

bool at32_clock_init(void)
{
	CRM->CTRL |= CRM_CTRL_HICKEN;
	if (!wait_set(&CRM->CTRL, CRM_CTRL_HICKSTBL, CRM_CTRL_HICKSTBL)) {
		return false;
	}
	// Back on the HICK first: the vendor bootloader may have left the PLL running, and the PLL can't be changed while
	// it runs (RM 4.3.2). SCLKSEL 0 = HICK
	CRM->MISC2 |= CRM_MISC2_AUTO_STEP_Msk;
	CRM->CFG &= ~CRM_CFG_SCLKSEL_Msk;
	(void)wait_set(&CRM->CFG, CRM_CFG_SCLKSTS_Msk, 0u);
	CRM->CTRL &= ~CRM_CTRL_PLLEN;
	// HICK_TO_SCLK 0: SCLK on the HICK is its fixed 8 MHz whatever HICKDIV says, so HICKDIV can change below. A step
	// down from 48 MHz if the bootloader had set it: never above what the flash's wait states allow
	CRM->MISC1 &= ~CRM_MISC1_HICK_TO_SCLK;
	SystemCoreClock = HICK_HZ;	// HICK_TO_SCLK = 0 at reset: SCLK on the HICK is a fixed 8 MHz (RM 4.3.23)

	// The flash's wait states before the clock goes up
	FLASHC->PSR = (FLASHC->PSR & ~FLASH_PSR_WTCYC_Msk) | FLASH_PSR_PFT_LAT_DIS | FLASH_PSR_PFT_EN | FLASH_PSR_PFT_EN2 |
		      FLASH_WTCYC;

	CRM->MISC1 &= ~CRM_MISC1_HICKDIV;	// the 8 MHz PLL input; only changed with the PLL off
	CRM->PLLCFG = (PLL_FR_CODE << CRM_PLLCFG_FR_Pos) | (PLL_NS << CRM_PLLCFG_NS_Pos) | (PLL_MS << CRM_PLLCFG_MS_Pos);	// PLLRCS 0: HICK
	CRM->CTRL |= CRM_CTRL_PLLEN;
	if (!wait_set(&CRM->CTRL, CRM_CTRL_PLLSTBL, CRM_CTRL_PLLSTBL)) {
		CRM->MISC2 &= ~CRM_MISC2_AUTO_STEP_Msk;
		return false;
	}
	CRM->CFG &= ~(CRM_CFG_AHBDIV_Msk | CRM_CFG_APB1DIV_Msk | CRM_CFG_APB2DIV_Msk);	// all undivided
	CRM->CFG = (CRM->CFG & ~CRM_CFG_SCLKSEL_Msk) | CRM_CFG_SCLKSEL_PLL;
	bool ok = wait_set(&CRM->CFG, CRM_CFG_SCLKSTS_Msk, CRM_CFG_SCLKSTS_PLL);
	CRM->MISC2 &= ~CRM_MISC2_AUTO_STEP_Msk;
	if (!ok) {
		return false;
	}
	SystemCoreClock = SCLK_HZ;
	pclk1 = SCLK_HZ;
	pclk2 = SCLK_HZ;
	return true;
}

uint32_t at32_pclk1_hz(void)
{
	return pclk1;
}

uint32_t at32_pclk2_hz(void)
{
	return pclk2;
}
