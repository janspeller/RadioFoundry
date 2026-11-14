// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SPI master on the registers (RM 13.4), byte at a time and polled (platform.h): no DMA or interrupt, so it works before
// the scheduler, and tx / rx may be NULL. The chip select is the board's GPIO
#include "platform.h"

#define XFER_SPINS	100000u		// per byte: a byte takes 0.44 us at 18 MHz

void at32_spi_master_init(SPI_Type *spi, bool cpol, bool cpha, uint8_t mdiv)
{
	if (spi == SPI1) {
		CRM->APB2EN |= CRM_APB2EN_SPI1EN;
	} else {
		CRM->APB1EN |= CRM_APB1EN_SPI2EN;
	}
	spi->CTRL1 = 0;
	spi->CTRL2 = 0;		// MDIV3EN clear: the divider is MDIV[2:0], 2^(mdiv + 1)
	// Master, software CS held inactive (SWCSEN, SWCSIL: RM 13.4.1 requires SWCSIL set in master mode), 8-bit, MSB
	// first
	spi->CTRL1 = SPI_CTRL1_MSTEN | SPI_CTRL1_SWCSEN | SPI_CTRL1_SWCSIL | ((uint32_t)(mdiv & 7u) << SPI_CTRL1_MDIV_Pos) |
		     (cpol ? SPI_CTRL1_CLKPOL : 0u) | (cpha ? SPI_CTRL1_CLKPHA : 0u);
	spi->CTRL1 |= SPI_CTRL1_SPIEN;
}

bool at32_spi_xfer(SPI_Type *r, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		uint32_t spins = XFER_SPINS;
		while ((r->STS & SPI_STS_TDBE) == 0) {
			if (--spins == 0) {
				return false;
			}
		}
		r->DT = tx != NULL ? tx[i] : 0xFFu;
		spins = XFER_SPINS;
		while ((r->STS & SPI_STS_RDBF) == 0) {
			if (--spins == 0) {
				return false;
			}
		}
		uint8_t b = (uint8_t)r->DT;
		if (rx != NULL) {
			rx[i] = b;
		}
	}
	uint32_t spins = XFER_SPINS;
	while ((r->STS & SPI_STS_BF) != 0) {
		if (--spins == 0) {
			return false;
		}
	}
	return true;
}
