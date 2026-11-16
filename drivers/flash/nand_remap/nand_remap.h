// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Read-only replacement-block log (docs/spec/nand-remap.md). No erase, program or reconstruction interface.
#ifndef NAND_REMAP_H
#define NAND_REMAP_H
#include <stdint.h>
#include <stdbool.h>
#define NAND_REMAP_MAX 89u
#define NAND_REMAP_BLOCK 0x20000u

typedef struct {
	bool (*read)(uint32_t addr, void *out, uint32_t len, void *ctx);
	void *ctx;
	uint16_t source[NAND_REMAP_MAX], target[NAND_REMAP_MAX], blocks;
	uint8_t count;
	bool ready;
} nand_remap_t;

// Caller validates the board's identification marker first. Empty marked logs are valid identity maps.
bool nand_remap_load(nand_remap_t *map,
	bool (*read)(uint32_t, void *, uint32_t, void *), void *ctx,
	uint16_t blocks, uint16_t replacement_first, uint16_t replacement_end);
// Resolve one address without issuing a read (also for explicitly flagged diagnostic reads).
bool nand_remap_address(const nand_remap_t *map, uint32_t logical, uint32_t *physical);
bool nand_remap_read(const nand_remap_t *map, uint32_t addr, void *out, uint32_t len);
#endif
