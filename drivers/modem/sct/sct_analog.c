// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SCT Packet Interface 1.2.42 and 2.5.3: FM needs an analog process mode and explicit call control.
#include <string.h>
#include "sct_analog.h"

#define ANSWER_MS 200u
#define VOCODER_MS 3000u
#define ERROR_SEND 0xFEu
#define ERROR_TIMEOUT 0xFFu

enum { VOCODER, MODE, SETUP, CONFIG, SUBAUDIO, RX_WORK, RX_CARRIER, RX_READY, TX_MIC, TX_WORK, TX_START, TX_READY, TX_STOP, RESTORE, DIGITAL_READY, FAILED };

void sct_analog_init(sct_analog_t *a, bool (*send)(const uint8_t *, uint16_t, void *), void *ctx)
{
	memset(a, 0, sizeof(*a));
	a->send = send;
	a->ctx = ctx;
	a->digital_ready = true;
}

void sct_analog_request(sct_analog_t *a, bool enabled, bool tx, bool narrow)
{
#if CONFIG_DIAG
	if (a->loopback) {
		a->loopback = false;
		a->restart = true;
		a->ready = false;
	}
#endif
	if (!enabled) {
		a->enabled = false;
		a->ready = false;
		if (a->phase == FAILED) {
			a->error = 0;
			a->phase = RESTORE;
		}
		return;
	}
	if (!a->enabled || a->narrow != narrow || a->phase == FAILED) {
		a->restart = true;
		a->ready = false;
		a->error = 0;
	}
	a->enabled = true;
	a->digital_ready = false;
	a->want_tx = tx;
	a->narrow = narrow;
}

#if CONFIG_DIAG
bool sct_analog_loopback(sct_analog_t *a, bool enabled)
{
	if (!a->enabled || !a->ready || a->pending || a->want_tx || a->phase != RX_READY) return false;
	a->loopback = enabled;
	a->ready = false;
	a->phase = RX_WORK;
	return true;
}
#endif

bool sct_analog_owns_link(const sct_analog_t *a)
{
	return a->enabled || !a->digital_ready || a->pending != 0;
}

static void send_command(sct_analog_t *a, const uint8_t *pkt, uint16_t len, uint8_t id,
			 uint8_t next, uint32_t now_ms, bool codec)
{
	if (len == 0 || a->send == NULL || !a->send(pkt, len, a->ctx)) {
		a->error = ERROR_SEND;
		a->phase = FAILED;
		a->ready = false;
		return;
	}
	a->commands++;
	a->last_id = id;
	a->pending = id;
	a->pending_codec = codec;
	a->next = next;
	a->sent_ms = now_ms;
	a->ready = false;
}

static void command(sct_analog_t *a, uint8_t type, uint8_t id, const uint8_t *data, uint8_t n,
		    uint8_t next, uint32_t now_ms)
{
	sct_pkt_t p;
	sct_pkt_begin(&p, type);
	sct_pkt_add(&p, id, data, n);
	uint16_t len = sct_pkt_end(&p, false);
	send_command(a, p.buf, len, id, next, now_ms, false);
}

void sct_analog_poll(sct_analog_t *a, uint32_t now_ms)
{
	if (!sct_analog_owns_link(a)) return;
	if (a->pending != 0) {
		if (now_ms - a->sent_ms >= (a->pending == SCT_F_VOCODER_SEL ? VOCODER_MS : ANSWER_MS)) {
			a->timeouts++;
			a->error = ERROR_TIMEOUT;
			a->phase = FAILED;
			a->pending = 0;
			a->ready = false;
		}
		return;
	}
	if (!a->enabled) {
		if (a->phase == FAILED) return;
		if (a->phase == TX_READY || a->phase == TX_START) {
			command(a, SCT_TYPE_CONTROL, SCT_F_CALL_STOP, NULL, 0, RESTORE, now_ms);
		} else {
			uint8_t vocoder = 4;
			command(a, SCT_TYPE_CONTROL, SCT_F_VOCODER_SEL, &vocoder, 1, DIGITAL_READY, now_ms);
		}
		return;
	}
	if (a->restart && a->phase == TX_READY) {
		command(a, SCT_TYPE_CONTROL, SCT_F_CALL_STOP, NULL, 0, MODE, now_ms);
		return;
	}
	if (a->restart) {
		a->restart = false;
		a->phase = a->analog_loaded ? MODE : VOCODER;
	}
	uint8_t data[11] = { 0 };
	switch (a->phase) {
	case VOCODER:
		data[0] = 16;
		command(a, SCT_TYPE_CONTROL, SCT_F_VOCODER_SEL, data, 1, MODE, now_ms);
		break;
	case MODE:
		data[0] = 0x80;
		a->setup_index = 0;
		command(a, SCT_TYPE_CONTROL, SCT_F_PROCESS_MODE, data, 1, SETUP, now_ms);
		break;
	case SETUP:
		if (a->setup_fields != NULL && a->setup_index < a->setup_count) {
			const sct_field_t *f = &a->setup_fields[a->setup_index++];
			command(a, SCT_TYPE_CONTROL, f->id, f->data, f->len, SETUP, now_ms);
			break;
		}
		a->phase = CONFIG;
		// fall through
	case CONFIG:
		// Standard narrow/wide channel filters, unity TX/RX gains in their documented Q formats.
		data[0] = a->narrow ? 0x00 : 0x0A;
		data[1] = 0x08; data[3] = 0x10; data[5] = 0x10; data[7] = 0x10; data[9] = 0x08;
		command(a, SCT_TYPE_CONTROL, SCT_F_ANALOG_CONFIG, data, 11, SUBAUDIO, now_ms);
		break;
	case SUBAUDIO:
		// The RF driver owns CTCSS/DCS. The baseband must not add or require a second tone.
		data[0] = 0x40;
		command(a, SCT_TYPE_CONTROL, SCT_F_SUBAUDIO, data, 2, RX_WORK, now_ms);
		break;
	case RX_WORK:
		data[0] = 1;
#if CONFIG_DIAG
		if (a->loopback) {
			// Packet Interface 2.14.3.1: codec loops, full duplex, no call start.
			data[0] = 3;
			data[1] = 0x11;
		}
#endif
		command(a, SCT_TYPE_CONTROL, SCT_F_WORK_MODE, data, 3, RX_CARRIER, now_ms);
		break;
	case RX_CARRIER:
		// Keep FM processing running; the radio core gates the speaker with its own squelch decision.
		data[0] = 1;
		command(a, SCT_TYPE_CONTROL, SCT_F_CARRIER_READY, data, 1, RX_READY, now_ms);
		break;
	case RX_READY:
		a->ready = true;
#if CONFIG_DIAG
		if (a->loopback) break;
#endif
		if (!a->want_tx) break;
		a->tx_codec_index = 0;
		a->phase = TX_MIC;
		// fall through
	case TX_MIC:
		if (!a->want_tx) {
			a->phase = RX_READY;
			a->ready = true;
			break;
		}
		if (a->tx_codec != NULL && a->tx_codec_index < a->tx_codec_count) {
			uint8_t pkt[SCT_CODEC_PKT_BYTES];
			sct_pkt_codec(pkt, &a->tx_codec[a->tx_codec_index]);
			send_command(a, pkt, sizeof(pkt), SCT_F_ACK, TX_MIC, now_ms, true);
			break;
		}
		a->phase = TX_WORK;
		// fall through
	case TX_WORK:
		data[0] = 2;
		command(a, SCT_TYPE_CONTROL, SCT_F_WORK_MODE, data, 3, TX_START, now_ms);
		break;
	case TX_START:
		if (!a->want_tx) {
			data[0] = 1;
			command(a, SCT_TYPE_CONTROL, SCT_F_WORK_MODE, data, 3, RX_CARRIER, now_ms);
			break;
		}
		command(a, SCT_TYPE_DMR, SCT_F_DMR_CALL_START, data, 0, TX_READY, now_ms);
		break;
	case TX_READY:
		a->ready = true;
		if (a->want_tx) break;
		a->phase = TX_STOP;
		// fall through
	case TX_STOP:
		command(a, SCT_TYPE_CONTROL, SCT_F_CALL_STOP, data, 0, RX_WORK, now_ms);
		break;
	default:
		break;
	}
}

void sct_analog_packet(sct_analog_t *a, const uint8_t *pkt, uint16_t len)
{
	if (!sct_analog_owns_link(a) || a->pending == 0 || len < SCT_HDR_BYTES) return;
	if (((pkt[5] & SCT_TYPE_CODEC) != 0) != a->pending_codec) return;
	sct_fields_t it;
	if (!sct_fields_begin(&it, pkt, len, false)) return;
	uint8_t id, n;
	const uint8_t *data;
	bool ack;
	while (sct_fields_next(&it, &id, &data, &n, &ack)) {
		if (n != 1 || ((!ack || id != a->pending) && id != SCT_F_ACK)) continue;
		if (id == SCT_F_ACK && data[0] == SCT_ACK_OK && !a->pending_codec) continue;
		a->error = data[0];
		if (a->error == 0 && a->pending == SCT_F_VOCODER_SEL) {
			a->analog_loaded = a->next == MODE;
			a->digital_ready = a->next == DIGITAL_READY;
		}
		if (a->error == 0 && a->pending_codec) a->tx_codec_index++;
		a->phase = a->error == 0 ? a->next : FAILED;
		a->pending = 0;
		return;
	}
}
