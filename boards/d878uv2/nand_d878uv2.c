// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS SPI NAND: a Kioxia TC58CVG1S3H (2 Gbit), bit-banged on GPIO (docs/spec/hardware-d878uv2.md 4.1).
// The same part the family's mobile carries, but on different pins: the clock is PB14 and the data out PB15, the
// other way round from that board, so no other board's NAND file may be copied here without changing them.
//
// The stock firmware's own writes reach byte address 0x0D9A0000, which is block 1741, so RadioFoundry's blocks
// (1792 up) sit above everything the vendor touches. The factory calibration is read from the chip and never
// written (hardware-d878uv2.md 5).
//
// One recursive mutex serializes every NAND transaction once the scheduler runs: the storage layer holds it across
// a whole read-modify-program sequence, which uses the chip's page buffer.
#include "d878uv2.h"
#include "config.h"
#include "pins.h"
#include "mcu_io.h"
#include "flash/spi_nand/spi_nand.h"
#include "hal_time.h"
#if CONFIG_WATCHDOG
#include "app/watchdog.h"
#endif
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static void chip_select(bool active)
{
	mcu_pin_write(PIN_NAND_CS, !active);
}

// Mode 0, MSB first: data out while the clock is low, sampled on the rising edge. GPIO stores at 120 MHz keep each
// half period well above the chip's 7.5 ns minimum
static uint8_t xfer_byte(uint8_t out)
{
	uint8_t in = 0;
	for (uint8_t m = 0x80; m != 0; m >>= 1) {
		mcu_pin_write(PIN_NAND_MOSI, (out & m) != 0);
		mcu_pin_write(PIN_NAND_CLK, true);
		if (mcu_pin_read(PIN_NAND_MISO)) {
			in |= m;
		}
		mcu_pin_write(PIN_NAND_CLK, false);
	}
	return in;
}

static bool xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		uint8_t v = xfer_byte(tx != NULL ? tx[i] : 0xFF);
		if (rx != NULL) {
			rx[i] = v;
		}
	}
	return true;
}

static const spi_bus_t bus = { .select = chip_select, .xfer = xfer };
// 2 KB pages with 64 spare bytes (on-die ECC on, its default), 64 pages per block, 2048 blocks
static spi_nand_t nand = {
	.bus = &bus,
	.page_size = 2048,
	.spare_size = 64,
	.pages_per_block = 64,
	.blocks = 2048,
#if CONFIG_NAND_STORAGE
	// Feature 0xB0: ECC on, the high speed read off (the internal data move needs it off). Every block locked
	// (0xA0 = 0x38) except during a program or erase. Writes only in RadioFoundry's ranges
	.config = 0x10,
	.lock_all = 0x38,
	.write_first = D878UV2_NAND_IDDB_FIRST,
	.write_blocks = D878UV2_NAND_IDDB_BLOCKS + D878UV2_NAND_RFOUNDRY_BLOCKS,
#endif
};
#if CONFIG_NAND_STORAGE
_Static_assert(D878UV2_NAND_IDDB_FIRST + D878UV2_NAND_IDDB_BLOCKS == D878UV2_NAND_RFOUNDRY_FIRST,
	       "the write window is one run of blocks: the ID database's range right below the sectors'");
_Static_assert(D878UV2_NAND_IDDB_FIRST * 64u * 2048u >= 0x0D9A0000u,
	       "RadioFoundry's blocks must start above everything the stock firmware writes");
#endif
static bool ready;
static SemaphoreHandle_t lock;

void d878uv2_nand_lock(void)
{
	if (lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		xSemaphoreTakeRecursive(lock, portMAX_DELAY);
	}
}

void d878uv2_nand_unlock(void)
{
	if (lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		xSemaphoreGiveRecursive(lock);
	}
}

bool d878uv2_nand_init(void)
{
	if (lock == NULL) {
		lock = xSemaphoreCreateRecursiveMutex();
	}
	mcu_pin_out(PIN_NAND_CS, true, false);
	mcu_pin_out(PIN_NAND_CLK, false, false);
	mcu_pin_out(PIN_NAND_MOSI, false, false);
	mcu_pin_in(PIN_NAND_MISO, MCU_PULL_UP);
	d878uv2_nand_lock();
	ready = spi_nand_probe(&nand);
	d878uv2_nand_unlock();
	return ready;
}

bool d878uv2_nand_ready(void)
{
	return ready;
}

uint16_t d878uv2_nand_id(void)
{
	return (uint16_t)(nand.manufacturer << 8 | nand.device);
}

bool d878uv2_nand_read(uint32_t addr, void *buf, uint32_t len)
{
	d878uv2_nand_lock();
	bool ok = ready && spi_nand_read(&nand, addr, buf, len);
	d878uv2_nand_unlock();
	return ok;
}

bool d878uv2_nand_read_any(uint32_t addr, void *buf, uint32_t len, uint32_t *ecc_failed)
{
	d878uv2_nand_lock();
	bool ok = ready && spi_nand_read_any(&nand, addr, buf, len, ecc_failed);
	d878uv2_nand_unlock();
	return ok;
}

#if CONFIG_NAND_STORAGE
// RadioFoundry's ranges as the layers see them (nand_dev_t): blocks and pages from the range's first block, the
// range in ctx. The driver's write window refuses anything outside both as well
typedef struct {
	uint16_t first, blocks;
} range_t;

static const range_t sectors_range = { D878UV2_NAND_RFOUNDRY_FIRST, D878UV2_NAND_RFOUNDRY_BLOCKS };
static const range_t iddb_range = { D878UV2_NAND_IDDB_FIRST, D878UV2_NAND_IDDB_BLOCKS };

#define PPB		64u
#define MAX_CHUNKS	2u

static uint32_t pages(const range_t *r)
{
	return (uint32_t)r->blocks * PPB;
}

static uint32_t first_page(const range_t *r)
{
	return (uint32_t)r->first * PPB;
}

static bool dev_read(void *ctx, uint32_t page, uint16_t column, void *buf, uint32_t len)
{
	const range_t *r = ctx;
	d878uv2_nand_lock();
	bool ok = ready && page < pages(r) && spi_nand_read_page(&nand, first_page(r) + page, column, buf, len);
	d878uv2_nand_unlock();
	return ok;
}

static nand_result_t result(spi_nand_result_t r)
{
	return r == SPI_NAND_DONE ? NAND_OK : r == SPI_NAND_FAILED ? NAND_FAILED : NAND_ERROR;
}

static nand_result_t dev_program(void *ctx, uint32_t page, uint32_t src, const nand_chunk_t *chunks, unsigned n)
{
	const range_t *rg = ctx;
	spi_nand_chunk_t c[MAX_CHUNKS];
	if (!ready || page >= pages(rg) || (src != NAND_SECTORS_NO_PAGE && src >= pages(rg)) || n > MAX_CHUNKS) {
		return NAND_ERROR;
	}
	for (unsigned i = 0; i < n; i++) {
		c[i].column = chunks[i].column;
		c[i].len = chunks[i].len;
		c[i].data = chunks[i].data;
	}
	d878uv2_nand_lock();
	spi_nand_result_t r = spi_nand_program(&nand, first_page(rg) + page,
					       src == NAND_SECTORS_NO_PAGE ? SPI_NAND_NO_PAGE : first_page(rg) + src,
					       c, n);
	d878uv2_nand_unlock();
	return result(r);
}

static nand_result_t dev_erase(void *ctx, uint16_t block)
{
	const range_t *rg = ctx;
	if (!ready || block >= rg->blocks) {
		return NAND_ERROR;
	}
	d878uv2_nand_lock();
	spi_nand_result_t r = spi_nand_erase(&nand, rg->first + block);
	d878uv2_nand_unlock();
	return result(r);
}

static bool dev_block_bad(void *ctx, uint16_t block, bool *bad)
{
	const range_t *rg = ctx;
	d878uv2_nand_lock();
	bool ok = ready && block < rg->blocks && spi_nand_block_bad(&nand, rg->first + block, bad);
	d878uv2_nand_unlock();
	return ok;
}

static void dev_mark_bad(void *ctx, uint16_t block)
{
	const range_t *rg = ctx;
	if (ready && block < rg->blocks) {
		d878uv2_nand_lock();
		spi_nand_mark_bad(&nand, rg->first + block);
		d878uv2_nand_unlock();
	}
}

// The sector layer's long loops keep the CPU with busy waits: the watchdog is fed per block or sector, and every
// DEV_BUSY_YIELD_MS of such work gives the other tasks one tick (docs/spec/watchdog.md 4)
#define DEV_BUSY_YIELD_MS	20u

static void dev_busy(void *ctx)
{
	static uint32_t last_yield;
	(void)ctx;
	uint32_t now = hal_millis();
#if CONFIG_WATCHDOG
	watchdog_kick(now);
#endif
	if (now - last_yield >= DEV_BUSY_YIELD_MS && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		vTaskDelay(1);
		last_yield = hal_millis();
	}
}

// On the family's mobile the factory block test leaves ten 0x55 bytes at the start of each block, the rest erased, and the
// sector layer treats such a block as blank. Whether this radio's factory leaves the same mark is **unconfirmed**:
// if a mount finds no blank blocks, this is the first thing to check (docs/testing/d878uv2-bringup.md, stage 4)
static const uint8_t factory_mark[10] = { 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };

const nand_dev_t d878uv2_nand_dev = {
	.ctx = (void *)&sectors_range,
	.pages_per_block = PPB,
	.blocks = D878UV2_NAND_RFOUNDRY_BLOCKS,
	.spare_size = 64,
	.read = dev_read,
	.program = dev_program,
	.erase = dev_erase,
	.block_bad = dev_block_bad,
	.mark_bad = dev_mark_bad,
	.busy = dev_busy,
	.factory_mark = factory_mark,
	.factory_mark_len = sizeof(factory_mark),
};

const nand_dev_t d878uv2_nand_iddb_dev = {
	.ctx = (void *)&iddb_range,
	.pages_per_block = PPB,
	.blocks = D878UV2_NAND_IDDB_BLOCKS,
	.spare_size = 64,
	.read = dev_read,
	.program = dev_program,
	.erase = dev_erase,
	.block_bad = dev_block_bad,
	.mark_bad = dev_mark_bad,
	.busy = dev_busy,
	.factory_mark = factory_mark,
	.factory_mark_len = sizeof(factory_mark),
};

#endif
