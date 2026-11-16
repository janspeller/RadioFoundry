// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include <string.h>
#include "sct_codec.h"

#define ANSWER_MS 200u
enum { NONE, BATCH, UPDATE };

void sct_codec_init(sct_codec_t *c, bool (*send)(const uint8_t *, uint16_t, void *), void *ctx)
{
	memset(c, 0, sizeof(*c));
	c->send = send;
	c->ctx = ctx;
}

bool sct_codec_begin(sct_codec_t *c, const sct_codec_write_t *writes, uint8_t count)
{
	if (c->fault || c->state == SCT_CODEC_BUSY || c->pending == BATCH || writes == NULL ||
	    count == 0 || count > SCT_CODEC_BATCH_MAX) return false;
	memcpy(c->batch, writes, count * sizeof(*writes));
	c->count = count;
	if (++c->generation == 0) c->generation++;
	c->acked = 0;
	c->error = 0;
	c->state = SCT_CODEC_BUSY;
	return true;
}

bool sct_codec_update(sct_codec_t *c, const sct_codec_write_t *write)
{
	if (c->fault || write == NULL) return false;
	for (uint8_t i = 0; i < c->queued; i++) {
		if (c->updates[i].page == write->page && c->updates[i].reg == write->reg) {
			c->updates[i] = *write;
			return true;
		}
	}
	if (c->queued == SCT_CODEC_UPDATES_MAX) return false;
	c->updates[c->queued++] = *write;
	return true;
}

void sct_codec_cancel(sct_codec_t *c)
{
	if (c->state == SCT_CODEC_BUSY) c->state = SCT_CODEC_IDLE;
	// Keep an in-flight write until its ACK drains; it cannot acknowledge a subsequent batch.
}

void sct_codec_poll(sct_codec_t *c, uint32_t now_ms)
{
	if (c->fault) return;
	if (c->pending != NONE) {
		if (now_ms - c->sent_ms >= ANSWER_MS) {
			c->timeouts++;
			c->error = 0xFF;
			c->state = SCT_CODEC_ERROR;
			c->fault = true;
		}
		return;
	}
	sct_codec_write_t write;
	if (c->state == SCT_CODEC_BUSY) {
		write = c->batch[c->acked];
		c->pending = BATCH;
	} else if (c->queued != 0) {
		write = c->updates[0];
		c->queued--;
		memmove(c->updates, c->updates + 1, c->queued * sizeof(write));
		c->pending = UPDATE;
	} else {
		return;
	}
	uint8_t pkt[SCT_CODEC_PKT_BYTES];
	sct_pkt_codec(pkt, &write);
	c->sent_ms = now_ms;
	if (c->send == NULL || !c->send(pkt, sizeof(pkt), c->ctx)) {
		c->error = 0xFE;
		c->state = SCT_CODEC_ERROR;
		c->fault = true;
	}
}

bool sct_codec_packet(sct_codec_t *c, const uint8_t *pkt, uint16_t len)
{
	sct_fields_t it;
	if (!sct_fields_begin(&it, pkt, len, false) || !(it.type & SCT_TYPE_CODEC)) return false;
	uint8_t id, n;
	const uint8_t *data;
	bool ack;
	while (sct_fields_next(&it, &id, &data, &n, &ack)) {
		if (id != SCT_F_ACK || n != 1 || c->pending == NONE) continue;
		uint8_t pending = c->pending;
		c->pending = NONE;
		if (c->fault) return true;
		if (data[0] != SCT_ACK_OK) {
			c->error = data[0];
			c->rejected++;
			c->state = SCT_CODEC_ERROR;
		} else if (pending == BATCH && c->state == SCT_CODEC_BUSY) {
			if (++c->acked == c->count) {
				c->completed = c->generation;
				c->state = SCT_CODEC_OK;
			}
		}
		return true;
	}
	return false;
}
