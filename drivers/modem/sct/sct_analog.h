// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Analog-call control for SCT basebands (docs/spec/modem-sct.md). Pure state machine; the board owns the bus.
#ifndef SCT_ANALOG_H
#define SCT_ANALOG_H
#include <stdbool.h>
#include <stdint.h>
#include "sct_pkt.h"
#include "config.h"

typedef struct {
	bool (*send)(const uint8_t *pkt, uint16_t len, void *ctx);
	void *ctx;
	// Board control fields sent once at each analog mode setup, before receive work mode.
	const sct_field_t *setup_fields;
	uint8_t setup_count, setup_index;
	// Optional board microphone routing/gain, reapplied and acknowledged before each TX work-mode request.
	const sct_codec_write_t *tx_codec;
	uint8_t tx_codec_count, tx_codec_index;
	bool pending_codec;
	bool enabled, want_tx, narrow, restart, ready, digital_ready, analog_loaded;
#if CONFIG_DIAG
	bool loopback;
#endif
	uint8_t phase, pending, next, error, last_id;
	uint32_t sent_ms, commands, timeouts;
} sct_analog_t;

void sct_analog_init(sct_analog_t *a, bool (*send)(const uint8_t *, uint16_t, void *), void *ctx);
// Disabled: restore the digital vocoder before relinquishing the link to the DMR engine.
void sct_analog_request(sct_analog_t *a, bool enabled, bool tx, bool narrow);
#if CONFIG_DIAG
// Codec loopback only while idle and ready. It never starts a call; ordinary radio requests cancel it.
bool sct_analog_loopback(sct_analog_t *a, bool enabled);
#endif
bool sct_analog_owns_link(const sct_analog_t *a);
void sct_analog_poll(sct_analog_t *a, uint32_t now_ms);
void sct_analog_packet(sct_analog_t *a, const uint8_t *pkt, uint16_t len);
#endif
