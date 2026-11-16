// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "flash/nand_remap/nand_remap.h"
static uint8_t metadata[0x20000];
static bool fail_read;
static uint32_t addresses[4], lengths[4];
static unsigned reads;
static bool raw_read(uint32_t addr, void *out, uint32_t n, void *ctx)
{
	(void)ctx;
	if (fail_read) return false;
	if (addr < sizeof(metadata) && n <= sizeof(metadata) - addr) {
		memcpy(out, metadata + addr, n);
	} else {
		assert(reads < 4);
		addresses[reads] = addr; lengths[reads++] = n;
		memset(out, (uint8_t)(addr >> 17), n);
	}
	return true;
}
static uint8_t *slot(unsigned index, unsigned count)
{
	uint8_t *p = metadata + index * 0x400;
	p[0x1F4] = 0x55; p[0x1F5] = (uint8_t)index;
	p[0x1F6] = (uint8_t)count; p[0x1F7] = 0;
	return p;
}
static void pair(uint8_t *p, unsigned i, unsigned source, unsigned target)
{
	p[2*i] = (uint8_t)source; p[2*i+1] = (uint8_t)(source >> 8);
	p[0xFA+2*i] = (uint8_t)target; p[0xFB+2*i] = (uint8_t)(target >> 8);
}
static bool load(nand_remap_t *m) { return nand_remap_load(m, raw_read, NULL, 2048, 0x180, 0x200); }
int main(void)
{
	nand_remap_t m = { 0 };
	uint8_t out[4];
	assert(!nand_remap_read(&m, 0, out, 1));
	memset(metadata, 0xFF, sizeof(metadata));
	assert(!load(&m)); // absent log is not an identity map
	slot(0, 0);
	assert(load(&m) && m.count == 0);
	assert(nand_remap_read(&m, 0x2F60000, out, 1));
	assert(addresses[0] == 0x2F60000);
	uint32_t resolved;
	assert(nand_remap_address(&m, 0x2F60000, &resolved) && resolved == 0x2F60000);
	assert(!nand_remap_address(&m, 0x10000000, &resolved));
	assert(!nand_remap_address(&m, 0, NULL));
	uint8_t *p = slot(1, 2);
	pair(p, 0, 0x17B, 0x180); pair(p, 1, 0x17C, 0x181);
	assert(load(&m) && m.count == 2);
	reads = 0;
	assert(nand_remap_read(&m, 0x2F7FFFE, out, 4));
	assert(nand_remap_address(&m, 0x2F60000, &resolved) && resolved == 0x3000000);
	assert(reads == 2 && addresses[0] == 0x301FFFE && addresses[1] == 0x3020000);
	assert(lengths[0] == 2 && lengths[1] == 2);
	assert(out[0] == 0x80 && out[2] == 0x81);
	pair(p, 1, 0x180, 0x181); assert(load(&m));
	reads = 0;
	assert(nand_remap_read(&m, 0x2F60000, out, 1));
	assert(addresses[0] == 0x3000000); // replace once; do not follow a replacement as another logical address
	pair(p, 1, 0x17C, 0x181); assert(load(&m));
	assert(!nand_remap_read(&m, 0xFFFFFFFF, out, 4));
	assert(!nand_remap_read(&m, 0x0FFFFFFF, out, 2));
	assert(!nand_remap_read(&m, 0, NULL, 1));
	pair(p, 1, 0x17B, 0x181); assert(!load(&m)); // duplicate source
	pair(p, 1, 0x17C, 0x180); assert(!load(&m)); // duplicate target
	pair(p, 1, 2048, 0x181); assert(!load(&m));
	pair(p, 1, 0x17C, 0x200); assert(!load(&m));
	pair(p, 1, 0x17C, 0x17F); assert(!load(&m));
	pair(p, 1, 0x181, 0x181); assert(!load(&m));
	pair(p, 1, 0x17C, 0x181);
	p[0x1F6] = 90; assert(!load(&m));
	p[0x1F6] = 2; p[0x1F5] = 0; assert(!load(&m));
	p[0x1F5] = 1; p[0x1F7] = 1; assert(!load(&m));
	p[0x1F7] = 0; assert(load(&m));
	fail_read = true; assert(!load(&m) && !m.ready && m.count == 0);
	fail_read = false;
	memset(metadata, 0xFF, sizeof(metadata));
	for (unsigned i = 0; i < 125; i++) slot(i, 0);
	assert(load(&m) && m.count == 0); // bounded full log
	assert(!nand_remap_load(&m, raw_read, NULL, 0xFFFF, 0x180, 0x200));
	puts("nandRemapTest PASSED");
	return 0;
}
