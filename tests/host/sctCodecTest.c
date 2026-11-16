// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Untagged codec ACKs must remain associated with one in-flight transaction.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modem/sct/sct_codec.h"

static uint8_t sent[32][SCT_CODEC_PKT_BYTES];
static unsigned count;
static bool fail;
static bool send_packet(const uint8_t *p, uint16_t n, void *ctx)
{
	(void)ctx;
	assert(n == SCT_CODEC_PKT_BYTES && count < 32);
	memcpy(sent[count++], p, n);
	return !fail;
}
static bool answer(sct_codec_t *c, uint8_t type, uint8_t code)
{
	const uint8_t p[] = {0x84,0xA9,0x61,0,2,type,0x17,code};
	return sct_codec_packet(c,p,sizeof(p));
}
static sct_codec_write_t mic[] = {{1,52,0x80},{1,59,52},{0,86,0xA0},{0,88,52}};

int main(void)
{
	sct_codec_t c;
	sct_codec_init(&c,send_packet,NULL);
	sct_codec_write_t volume = {1,16,1};
	assert(sct_codec_update(&c,&volume));
	sct_codec_poll(&c,0);
	assert(count == 1 && sent[0][8] == 16);
	volume.value = 2; assert(sct_codec_update(&c,&volume));
	volume.value = 3; assert(sct_codec_update(&c,&volume) && c.queued == 1);
	assert(sct_codec_begin(&c,mic,4));
	sct_codec_poll(&c,1);
	assert(count == 1); // an old volume ACK must drain before any microphone write
	assert(!answer(&c,0,0));
	sct_codec_poll(&c,2); assert(count == 1 && c.acked == 0);
	assert(answer(&c,0x40,0));
	for (unsigned i = 0; i < 4; i++) {
		sct_codec_poll(&c,3+i);
		assert(count == i+2 && sent[i+1][7] == mic[i].page && sent[i+1][8] == mic[i].reg && sent[i+1][9] == mic[i].value);
		assert(c.state == SCT_CODEC_BUSY && c.acked == i);
		assert(answer(&c,0x40,0));
		if (i < 3) assert(c.completed != c.generation);
	}
	assert(c.state == SCT_CODEC_OK && c.acked == 4 && c.completed == c.generation);
	sct_codec_poll(&c,10);
	assert(count == 6 && sent[5][8] == 16 && sent[5][9] == 3);
	assert(answer(&c,0x40,0));
	// A canceled write still owns its ACK; it cannot complete a subsequent preparation.
	assert(sct_codec_begin(&c,mic,4)); sct_codec_poll(&c,20);
	assert(c.completed != c.generation);
	sct_codec_cancel(&c);
	assert(!sct_codec_begin(&c,mic,4));
	assert(answer(&c,0x40,0) && c.state == SCT_CODEC_IDLE);
	assert(c.completed != c.generation);
	assert(sct_codec_begin(&c,mic,4)); sct_codec_poll(&c,21);
	assert(answer(&c,0x40,6));
	assert(c.state == SCT_CODEC_ERROR && c.error == 6 && c.rejected == 1 && c.acked == 0);
	assert(sct_codec_begin(&c,mic,4)); // an explicit NAK has drained; a fresh batch can retry
	sct_codec_poll(&c,UINT32_MAX-100u);
	sct_codec_poll(&c,98); assert(!c.fault);
	sct_codec_poll(&c,99); assert(c.fault && c.error == 0xFF && c.timeouts == 1);
	assert(answer(&c,0x40,0));
	assert(!sct_codec_begin(&c,mic,4) && !sct_codec_update(&c,&volume));
	unsigned before = count;
	sct_codec_poll(&c,1000); assert(count == before); // late ACKs cannot revive a timed-out stream
	sct_codec_init(&c,send_packet,NULL);
	fail = true;
	assert(sct_codec_begin(&c,mic,4)); sct_codec_poll(&c,0);
	assert(c.fault && c.error == 0xFE && c.state == SCT_CODEC_ERROR);
	puts("sctCodecTest PASSED");
	return 0;
}
