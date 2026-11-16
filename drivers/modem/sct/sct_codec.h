// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Serialized runtime codec transactions: codec ACKs carry no register or transaction identifier.
#ifndef SCT_CODEC_H
#define SCT_CODEC_H
#include "sct_pkt.h"

#define SCT_CODEC_BATCH_MAX 4u
#define SCT_CODEC_UPDATES_MAX 4u
enum { SCT_CODEC_IDLE, SCT_CODEC_BUSY, SCT_CODEC_OK, SCT_CODEC_ERROR };

typedef struct {
	bool (*send)(const uint8_t *, uint16_t, void *);
	void *ctx;
	sct_codec_write_t batch[SCT_CODEC_BATCH_MAX], updates[SCT_CODEC_UPDATES_MAX];
	uint8_t state, error, count, acked, queued, pending;
	bool fault; // A timeout or uncertain send fences late ACKs until the modem is reset.
	uint32_t sent_ms, timeouts, rejected;
	uint32_t generation;
	volatile uint32_t completed;	// Published only after every batch write is acknowledged; safe for a slot ISR.
} sct_codec_t;

// Initialize at modem reset/boot, not to clear a runtime timeout with potentially late ACKs still in flight.
void sct_codec_init(sct_codec_t *c, bool (*send)(const uint8_t *, uint16_t, void *), void *ctx);
bool sct_codec_begin(sct_codec_t *c, const sct_codec_write_t *writes, uint8_t count);
bool sct_codec_update(sct_codec_t *c, const sct_codec_write_t *write);
void sct_codec_cancel(sct_codec_t *c);
void sct_codec_poll(sct_codec_t *c, uint32_t now_ms);
bool sct_codec_packet(sct_codec_t *c, const uint8_t *pkt, uint16_t len);
#endif
