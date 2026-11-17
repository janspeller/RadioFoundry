// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SCT raw-I2C boot diagnostics against an independent byte-oriented device model.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modem/sct/sct_host.h"

static struct {
	uint8_t in[512], page, reply_type;
	uint16_t pos, len, chunk;
	uint32_t now;
	bool silent, fail, short_reply;
	unsigned writes;
} bus;
static void reply_status(uint8_t id, uint8_t status)
{
	uint8_t pkt[] = { 0x84, 0xA9, 0x61, 0, 2, 0, id, status };
	memcpy(bus.in, pkt, sizeof(pkt)); bus.pos = 0; bus.len = sizeof(pkt);
}
static bool write_bus(const uint8_t *pkt, uint16_t len, void *ctx)
{
	(void)ctx;
	bus.writes++;
	assert(pkt[0] == 0x84 && pkt[1] == 0xA9 && pkt[2] == 0x61 && pkt[6] == 0x6A);
	assert(pkt[7] == 0x30);
	if (bus.silent) return true;
	if (bus.fail) { reply_status(0x17, 7); return true; }
	if (pkt[5] == 0) {
		assert(len == 12 && pkt[4] == 5 && pkt[8] == 0 && pkt[9] == 1 && pkt[11] == 0);
		bus.page = pkt[10];
		reply_status(0x6A, 0);
	} else {
		assert(len == 10 && pkt[4] == 4 && pkt[5] == 0x20);
		unsigned count = pkt[9] - (bus.short_reply ? 1u : 0u);
		unsigned body = count + 1;
		bus.in[0] = 0x84; bus.in[1] = 0xA9; bus.in[2] = 0x61;
		bus.in[3] = (uint8_t)(body >> 8); bus.in[4] = (uint8_t)body;
		bus.in[5] = bus.reply_type; bus.in[6] = 0x6A;
		for (unsigned i = 0; i < count; i++) bus.in[7 + i] = (uint8_t)(pkt[8] + i + bus.page * 128);
		bus.len = (uint16_t)((7 + count + 1) & ~1u);
		if ((7 + count) & 1u) bus.in[7 + count] = 0;
		bus.pos = 0;
	}
	return true;
}
static uint16_t read_bus(uint8_t *out, uint16_t cap, void *ctx)
{
	(void)ctx;
	unsigned n = bus.len - bus.pos;
	if (n > cap) n = cap;
	if (n > bus.chunk) n = bus.chunk;
	memcpy(out, bus.in + bus.pos, n);
	bus.pos += (uint16_t)n;
	return (uint16_t)n;
}
static uint32_t now(void) { return bus.now; }
static void delay(uint32_t ms) { bus.now += ms; }
static const sct_host_t host = { .write = write_bus, .read = read_bus, .now_ms = now, .delay_ms = delay };
int main(void)
{
	sct_rx_t rx;
	uint8_t out[248];
	memset(&bus, 0, sizeof(bus)); bus.chunk = 1; bus.reply_type = 0x20;
	sct_rx_reset(&rx);
	assert(sct_host_i2c_page(&host, &rx, 0x30, 1, 200));
	assert(sct_host_i2c_read(&host, &rx, 0x30, 0, out, 128, 200));
	for (unsigned i = 0; i < 128; i++) assert(out[i] == (uint8_t)(128 + i));
	assert(sct_host_i2c_page(&host, &rx, 0x30, 0, 200));
	bus.chunk = 64;
	assert(sct_host_i2c_read(&host, &rx, 0x30, 7, out, sizeof(out), 200));
	for (unsigned i = 0; i < sizeof(out); i++) assert(out[i] == (uint8_t)(7 + i));
	// Reports retain the read bit when a protocol selector is present.
	const uint8_t reports[] = { 0x20, 0x23, 0x25 };
	for (unsigned t = 0; t < sizeof(reports); t++) {
		bus.reply_type = reports[t];
		assert(sct_host_i2c_read(&host, &rx, 0x30, 7, out, 16, 200));
		for (unsigned i = 0; i < 16; i++) assert(out[i] == (uint8_t)(7 + i));
	}
	const uint8_t controls[] = { 0x00, 0x03, 0x05 };
	for (unsigned t = 0; t < sizeof(controls); t++) {
		bus.reply_type = controls[t];
		memset(out, 0xCC, sizeof(out));
		assert(!sct_host_i2c_read(&host, &rx, 0x30, 7, out, 16, 200));
		for (unsigned i = 0; i < sizeof(out); i++) assert(out[i] == 0xCC);
	}
	bus.reply_type = 0x20;
	unsigned writes = bus.writes;
	assert(!sct_host_i2c_read(&host, &rx, 0x30, 0, out, 0, 200));
	assert(!sct_host_i2c_read(&host, &rx, 0x30, 0, NULL, 2, 200));
	assert(!sct_host_i2c_read(&host, &rx, 0x30, 0, out, 255, 200));
	assert(bus.writes == writes);
	bus.fail = true;
	assert(!sct_host_i2c_page(&host, &rx, 0x30, 1, 200));
	assert(!sct_host_i2c_read(&host, &rx, 0x30, 0, out, 16, 200));
	bus.fail = false; bus.short_reply = true;
	memset(out, 0xCC, sizeof(out));
	assert(!sct_host_i2c_read(&host, &rx, 0x30, 0, out, 16, 200));
	assert(bus.now >= 200);
	for (unsigned i = 0; i < sizeof(out); i++) assert(out[i] == 0xCC);
	bus.short_reply = false; bus.silent = true;
	bus.now = UINT32_MAX - 50u;
	assert(!sct_host_i2c_read(&host, &rx, 0x30, 0, out, 16, 200));
	assert(bus.now == 149u);
	puts("sctI2cTest PASSED");
	return 0;
}
