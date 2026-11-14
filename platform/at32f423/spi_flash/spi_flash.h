// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// An SPI NOR data flash on an AT32F423 SPI controller with a GPIO chip select, as hal_flash_t (spi_flash.c). A board
// lists platform/at32f423/spi_flash and drivers/flash/spi_nor in its BOARD_DIRS and configures it before
// at32_spi_flash.init() runs. Writes and erases below write_floor are refused (the radio's factory data). map: the core's
// areas, dropped when the chip is too small for them
#ifndef AT32_SPI_FLASH_H
#define AT32_SPI_FLASH_H

#include "platform.h"
#include "hal_flash.h"

typedef struct {
	SPI_Type *spi;
	uint8_t mdiv;			// SPI clock PCLK / 2^(mdiv + 1)
	GPIO_Type *bus_port;		// SCK, MISO and MOSI, all on one port
	uint16_t bus_pins;
	uint8_t mux;			// their IOMUX function
	GPIO_Type *cs_port;
	uint16_t cs_pin;
	uint32_t write_floor;
	const hal_flash_map_t *map;
} at32_spi_flash_cfg_t;

void at32_spi_flash_configure(const at32_spi_flash_cfg_t *cfg);
extern hal_flash_t at32_spi_flash;
uint16_t at32_spi_flash_device(void);	// JEDEC memory type << 8 | capacity, 0 before a good probe

#endif
