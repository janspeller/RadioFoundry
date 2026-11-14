// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The HF6853's frame: build, checksum, parse (docs/spec/hardware-rt4d.md 5.2)
#include "hf6853_frame.h"

uint16_t hf6853_checksum(const uint8_t *frame, size_t len)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < len; i += 2) {
		if (i == 4) {
			continue;	// the checksum field, taken as zero
		}
		uint32_t w = (uint32_t)frame[i] << 8;
		if (i + 1 < len) {
			w |= frame[i + 1];
		}
		sum += w;
	}
	while (sum >> 16) {
		sum = (sum & 0xFFFFu) + (sum >> 16);
	}
	return (uint16_t)~sum;
}

size_t hf6853_frame_build(uint8_t *f, uint8_t cmd, uint8_t rw, uint8_t sr, uint16_t len, bool check)
{
	f[0] = HF6853_HEAD;
	f[1] = cmd;
	f[2] = rw;
	f[3] = sr;
	f[4] = f[5] = 0;
	f[6] = (uint8_t)(len >> 8);
	f[7] = (uint8_t)len;
	size_t n = HF6853_HDR + len;
	f[n++] = HF6853_TAIL;
	if (check) {
		uint16_t c = hf6853_checksum(f, n);
		f[4] = (uint8_t)(c >> 8);
		f[5] = (uint8_t)c;
	}
	return n;
}

bool hf6853_rx_byte(hf6853_rx_t *rx, uint8_t b)
{
	if (rx->skip != 0) {
		rx->skip--;
		return false;
	}
	if (rx->pos == 0 && b != HF6853_HEAD) {
		return false;		// between frames: look for a head
	}
	rx->buf[rx->pos++] = b;
	if (rx->pos < HF6853_HDR) {
		return false;
	}
	uint16_t len = hf6853_rx_len(rx);
	if (len > HF6853_RX_DATA_MAX) {
		rx->too_long++;
		rx->skip = (uint16_t)(len + 1u);	// its DATA and tail
		rx->pos = 0;
		return false;
	}
	size_t total = HF6853_HDR + len + 1u;
	if (rx->pos < total) {
		return false;
	}
	rx->pos = 0;
	uint16_t got = (uint16_t)(rx->buf[4] << 8 | rx->buf[5]);
	if (b != HF6853_TAIL || (got != 0 && got != hf6853_checksum(rx->buf, total))) {
		rx->bad++;
		return false;
	}
	if (got == 0) {
		rx->unchecked++;
	}
	return true;
}
