// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include "nand_remap.h"
#include <stddef.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

bool nand_remap_load(nand_remap_t *m,
	bool (*read)(uint32_t, void *, uint32_t, void *), void *ctx,
	uint16_t blocks, uint16_t first, uint16_t end)
{
	m->ready = false;
	m->count = 0;
	m->read = read;
	m->ctx = ctx;
	m->blocks = blocks;
	if (read == NULL || blocks > UINT32_MAX / NAND_REMAP_BLOCK || first >= end || end > blocks) return false;
	unsigned slots = 0;
	for (; slots < 125; slots++) {
		uint8_t marker;
		if (!read(slots * 0x400u + 0x1F4u, &marker, 1, ctx)) return false;
		if (marker != 0x55) break;
	}
	if (slots == 0) return false;
	uint8_t record[0x1F8];
	if (!read((slots - 1u) * 0x400u, record, sizeof(record), ctx)) return false;
	unsigned count = record[0x1F6];
	if (record[0x1F4] != 0x55 || record[0x1F5] != slots - 1u ||
	    record[0x1F7] != 0 || count > NAND_REMAP_MAX) return false;
	for (unsigned i = 0; i < count; i++) {
		uint16_t src = le16(record + 2u * i), dst = le16(record + 0xFAu + 2u * i);
		if (src >= blocks || dst < first || dst >= end || src == dst) return false;
		for (unsigned j = 0; j < i; j++) {
			if (src == m->source[j] || dst == m->target[j]) return false;
		}
		m->source[i] = src; m->target[i] = dst;
	}
	m->count = (uint8_t)count;
	m->ready = true;
	return true;
}

bool nand_remap_read(const nand_remap_t *m, uint32_t addr, void *out, uint32_t len)
{
	uint32_t size = (uint32_t)m->blocks * NAND_REMAP_BLOCK;
	if (!m->ready || out == NULL || addr > size || len > size - addr) return false;
	uint8_t *p = out;
	while (len != 0) {
		uint32_t block = addr / NAND_REMAP_BLOCK, offset = addr % NAND_REMAP_BLOCK;
		uint32_t n = NAND_REMAP_BLOCK - offset;
		if (n > len) n = len;
		for (unsigned i = 0; i < m->count; i++) {
			if (m->source[i] == block) { block = m->target[i]; break; }
		}
		if (!m->read(block * NAND_REMAP_BLOCK + offset, p, n, m->ctx)) return false;
		addr += n; p += n; len -= n;
	}
	return true;
}
