// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The data flash of an AT32F423 radio: an SPI NOR (drivers/flash/spi_nor) on one of the SPI controllers with a GPIO chip
// select, as hal_flash_t (spi_flash.h). A mutex serializes the tasks once the scheduler runs.
#include "spi_flash.h"
#include "flash/spi_nor/spi_nor.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static const at32_spi_flash_cfg_t *cfg;
static SemaphoreHandle_t lock;
static bool ready;

static void chip_select(bool active)
{
	at32_pin_write(cfg->cs_port, cfg->cs_pin, !active);
}

static bool xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	return at32_spi_xfer(cfg->spi, tx, rx, len);
}

static const spi_bus_t bus = { .select = chip_select, .xfer = xfer };
static spi_nor_t nor = { .bus = &bus };

static void take(void)
{
	if (lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		xSemaphoreTake(lock, portMAX_DELAY);
	}
}

static void give(void)
{
	if (lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		xSemaphoreGive(lock);
	}
}

static bool init(void)
{
	if (cfg == NULL) {
		return false;
	}
	if (lock == NULL) {
		lock = xSemaphoreCreateMutex();
	}
	at32_pin_out(cfg->cs_port, cfg->cs_pin, true, false);
	at32_pin_mux(cfg->bus_port, cfg->bus_pins, cfg->mux, false, GPIO_PULL_NONE);
	at32_spi_master_init(cfg->spi, false, false, cfg->mdiv);	// mode 0
	take();
	bool ok = spi_nor_probe(&nor) && nor.size != 0;
	give();
	ready = ok;
	at32_spi_flash.size = ok ? nor.size : 0;
	// No map on a chip too small for it; a chip that didn't answer keeps it with size 0 (the debug link tells "didn't
	// start" from "too small" by it)
	at32_spi_flash.map = !ok || hal_flash_map_end(cfg->map, SPI_NOR_SECTOR_SIZE) <= nor.size ? cfg->map : NULL;
	return ok;
}

void at32_spi_flash_configure(const at32_spi_flash_cfg_t *c)
{
	cfg = c;
}

static bool in_range(uint32_t addr, uint32_t len)
{
	return ready && addr < nor.size && len <= nor.size - addr;
}

static bool rd(uint32_t addr, void *buf, uint32_t len)
{
	if (!in_range(addr, len)) {
		return false;
	}
	take();
	bool ok = spi_nor_read(&nor, addr, buf, len);
	give();
	return ok;
}

// Writes and erases below the floor are refused: the radio's factory data (its calibration) lives there
static bool wr(uint32_t addr, const void *buf, uint32_t len)
{
	if (!in_range(addr, len) || addr < cfg->write_floor) {
		return false;
	}
	take();
	bool ok = spi_nor_program(&nor, addr, buf, len);
	give();
	return ok;
}

static bool er(uint32_t addr)
{
	if (!in_range(addr, 1) || addr < cfg->write_floor) {
		return false;
	}
	take();
	bool ok = spi_nor_erase_sector(&nor, addr);
	give();
	return ok;
}

hal_flash_t at32_spi_flash = {
	.init = init,
	.size = 0,
	.erase_size = SPI_NOR_SECTOR_SIZE,
	.read = rd,
	.write = wr,
	.erase = er,
	.map = NULL,	// set by init
};

uint16_t at32_spi_flash_device(void)
{
	return ready ? nor.device : 0;
}
