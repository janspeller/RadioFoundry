// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The call-level DMR backend (docs/spec/dmr-call-level.md): the call stack's interface (dmr/call/dmr_call.h), the one
// the radio core drives, over a chip that runs calls itself (board.dmr_call, hal_dmr_call.h). The radio core's talk
// target, reply window, focus and names work unchanged; this file turns its configuration into the chip's channel,
// receive groups and key, PTT into call start and stop, and the chip's events into the call view.
//
// Threading: everything runs on the radio task. The chip's events come from hal->poll() inside dmr_call_poll(), so
// they are handled at once, without a queue. Nothing blocks.
#include <string.h>
#include "config.h"
#include "board.h"
#include "dmr/call/dmr_call.h"
#include "dmr/call_level/dmr_call_level.h"

const uint8_t dmr_ambe_silence[DMR_AMBE_FRAME_BYTES] = { 0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B };

static struct {
	const hal_dmr_call_t *hal;
	dmr_call_config_t cfg;
	dmr_call_view_t view;
	dmr_call_stats_t stats;
	bool running;
	bool ready;		// DMRC_EV_READY since the chip was powered
	bool pushed;		// the channel went to the chip since it was ready or changed
	bool ptt;		// PTT held
	uint8_t tx;		// dmrcl_tx_t
	bool rx;		// a call heard (DMRC_EV_CALL start without end)
	uint32_t now;
	uint32_t tx_start_ms;
	dmr_target_t tx_to;	// the target of the call we are sending
	dmr_target_t reply;
	bool reply_set;
	uint32_t reply_until;
	uint32_t last_activity_ms;
	bool activity;
	dmrcl_info_t info;
} s;

typedef enum { TXL_IDLE, TXL_ASKED, TXL_ON, TXL_DENIED } dmrcl_tx_t;

static bool before(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) < 0;
}

static bool same_target(dmr_target_t a, dmr_target_t b)
{
	return a.id == b.id && a.private_call == b.private_call;
}

static uint32_t caps(void)
{
	return s.hal != NULL ? s.hal->caps : 0;
}

static uint8_t reply_window_s(bool private_call)
{
	return private_call && s.cfg.reply_window_private_s ? s.cfg.reply_window_private_s : s.cfg.reply_window_s;
}

static bool reply_running(void)
{
	return s.reply_set && reply_window_s(s.reply.private_call) && before(s.now, s.reply_until) &&
		!same_target(s.reply, s.cfg.talk_target);
}

static dmr_target_t effective_target(void)
{
	return reply_running() ? s.reply : s.cfg.talk_target;
}

static void dirty(void)
{
	s.view.serial++;
}

// A clear the compiler can't drop as a dead store
static void wipe(void *p, size_t n)
{
	volatile uint8_t *b = p;
	while (n-- != 0) {
		*b++ = 0;
	}
}

static void load_key(void)
{
	const hal_dmr_call_t *h = s.hal;
	if (!CONFIG_DMR_AES || h->key == NULL) {
		return;
	}
	dmrc_key_t k;		// on the stack only while it goes to the chip (dmr-call-level.md 6)
	bool have = s.cfg.key_number != 0 && h->key_get != NULL && h->key_get(s.cfg.key_number, &k) &&
		k.type != DMRC_KEY_OFF;
	s.info.key_number = have ? s.cfg.key_number : 0;
	s.info.key_type = have ? k.type : DMRC_KEY_OFF;
	s.info.key_loaded = false;
	h->key(have ? &k : NULL);
	wipe(&k, sizeof(k));
}

// The channel, receive groups and key to the chip (the order the chip's driver sends them in is its own)
static void push(void)
{
	const hal_dmr_call_t *h = s.hal;
	if (!s.running || !s.ready || h == NULL) {
		return;
	}
	dmr_target_t t = effective_target();
	dmrc_channel_t ch = {
		.rx_hz = s.cfg.rx_hz, .tx_hz = s.cfg.tx_hz != 0 ? s.cfg.tx_hz : s.cfg.rx_hz,
		.cc = s.cfg.cc, .ts = s.cfg.ts, .rmo = s.cfg.rmo, .power = s.cfg.power,
		.own_id = s.cfg.own_id, .tx_id = t.id, .tx_private = t.private_call,
		.key_number = CONFIG_DMR_AES ? s.cfg.key_number : 0,
		.hear_all = s.cfg.listen.hear_all,
	};
	uint8_t n = s.cfg.listen.group_count > 255u ? 255u : (uint8_t)s.cfg.listen.group_count;
	bool ok = h->channel(&ch);
	ok = h->groups(s.cfg.listen.groups, s.cfg.listen.groups != NULL ? n : 0) && ok;
	load_key();
	s.pushed = ok;
	if (!ok) {
		s.info.push_fails++;
	}
}

static void event(const dmrc_event_t *ev, void *ctx)
{
	(void)ctx;
	switch (ev->type) {
	case DMRC_EV_READY:
		s.ready = true;
		s.info.version = ev->u.ready.version;
		s.info.fault = 0;
		push();
		break;
	case DMRC_EV_FAULT:
		s.ready = false;
		s.info.fault = ev->u.fault.reason;
		s.info.faults++;
		if (s.tx == TXL_ASKED || s.tx == TXL_ON) {
			s.tx = s.ptt ? TXL_DENIED : TXL_IDLE;
			s.view.tx_error = DMR_TX_ERR_REJECTED;
		}
		break;
	case DMRC_EV_STATE:
		if (ev->u.state == DMRC_ST_TX && s.tx == TXL_ASKED) {
			s.tx = TXL_ON;
			s.tx_start_ms = s.now;
			s.view.call_start_ms = s.now;
		} else if (ev->u.state == DMRC_ST_TX_REFUSED && (s.tx == TXL_ASKED || s.tx == TXL_ON)) {
			s.tx = s.ptt ? TXL_DENIED : TXL_IDLE;
			s.view.tx_error = DMR_TX_ERR_BUSY;
			s.stats.tx_denied++;
		} else if (ev->u.state == DMRC_ST_IDLE && s.tx == TXL_ON && !s.ptt) {
			s.tx = TXL_IDLE;
		}
		break;
	case DMRC_EV_CALL: {
		dmr_caller_t *c = &s.view.caller;
		if (ev->u.call.start) {
			memset(c, 0, sizeof(*c));
			c->src = ev->u.call.src;
			c->dst = ev->u.call.dst;
			c->group = ev->u.call.call_type != DMRC_CALL_PRIVATE;
			c->to_us = !c->group && c->dst == s.cfg.own_id;
			c->encrypted = ev->u.call.encrypted;
			c->emergency = ev->u.call.emergency;
			if (!s.rx) {
				s.view.call_start_ms = s.now;
				s.stats.rx_calls++;
				s.stats.rx_calls_played++;
				s.stats.rx_private_calls += !c->group;
				s.stats.rx_encrypted_calls += c->encrypted;
				s.stats.rx_emergency_calls += c->emergency;
			}
			s.rx = true;
		} else if (s.rx) {
			s.rx = false;
			s.view.last_heard = *c;
			s.view.last_heard_ms = s.now != 0 ? s.now : 1;
			// The reply window: back to the caller (a private call to us), else to the talkgroup heard
			dmr_target_t t = { c->group ? c->dst : c->src, !c->group };
			uint8_t w = reply_window_s(t.private_call);
			if (w != 0 && (c->group || c->to_us)) {
				s.reply = t;
				s.reply_until = s.now + (uint32_t)w * 1000u;
				s.reply_set = true;
			}
		}
		s.last_activity_ms = s.now;
		s.activity = true;
		break;
	}
	case DMRC_EV_RSSI:
		s.view.rssi_dbm = ev->u.rssi_dbm;
		break;
	case DMRC_EV_BER:
		s.info.ber_pct = ev->u.ber_pct;
		break;
	case DMRC_EV_KEY:
		s.info.key_loaded = ev->u.key_ok && s.info.key_number != 0;
		break;
	}
	dirty();
}

void dmr_call_config_default(dmr_call_config_t *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->cc = 1;
	cfg->ts = 1;
	cfg->talk_target.id = 9;
	cfg->reply_window_s = DMR_CALL_REPLY_WINDOW_S;
	cfg->cc2 = -1;
}

void dmr_call_init(const hal_dmr_modem_t *modem)
{
	(void)modem;		// a call-level radio has no burst-level modem: the chip is board.dmr_call
	memset(&s, 0, sizeof(s));
	s.hal = board.dmr_call;
	dmr_call_config_default(&s.cfg);
	s.view.cc = s.cfg.cc;
	s.view.ts = s.cfg.ts;
	if (s.hal != NULL) {
		if (s.hal->init != NULL) {
			s.hal->init();
		}
		s.hal->listen(event, NULL);
	}
}

void dmr_call_set_voice(const dmr_voice_sink_t *sink, const dmr_voice_source_t *source)
{
	(void)sink;
	(void)source;		// the chip has the vocoder
}

void dmr_call_configure(const dmr_call_config_t *cfg)
{
	bool changed = cfg->cc != s.cfg.cc || cfg->ts != s.cfg.ts || cfg->rmo != s.cfg.rmo || cfg->rx_hz != s.cfg.rx_hz;
	s.cfg = *cfg;
	if (s.cfg.reply_window_s > 30)
		s.cfg.reply_window_s = 30;
	if (s.cfg.reply_window_private_s > 30)
		s.cfg.reply_window_private_s = 30;
	s.cfg.listen.group_ok = NULL;	// the chip filters by its group list alone
	if (changed) {
		s.rx = false;		// a new channel: the call heard on the old one is over
	}
	s.view.cc = s.cfg.cc;
	s.view.ts = s.cfg.ts;
	s.view.rmo = s.cfg.rmo;
	push();
	dirty();
}

bool dmr_call_start(void)
{
	if (s.hal == NULL) {
		return false;
	}
	if (!s.running) {
		s.running = true;
		s.ready = s.hal->is_ready != NULL && s.hal->is_ready();
		s.hal->power(true);
	}
	push();
	return true;
}

void dmr_call_stop(void)
{
	if (s.hal == NULL || !s.running) {
		return;
	}
	if (s.tx == TXL_ASKED || s.tx == TXL_ON) {
		dmr_target_t t = s.tx_to;
		s.hal->call(t.private_call ? DMRC_CALL_PRIVATE : DMRC_CALL_GROUP, t.id, false);
	}
	s.hal->power(false);
	s.running = s.ready = s.pushed = s.ptt = s.rx = false;
	s.tx = TXL_IDLE;
	s.reply_set = false;
	memset(&s.view.caller, 0, sizeof(s.view.caller));
	dirty();
}

static dmrc_call_type_t call_type(dmr_target_t t)
{
	if (t.private_call) {
		return DMRC_CALL_PRIVATE;
	}
	return t.id == DMR_ID_ALL_CALL && (caps() & DMRC_CAP_ALL_CALL) != 0 ? DMRC_CALL_ALL : DMRC_CALL_GROUP;
}

void dmr_call_ptt(bool down)
{
	if (down == s.ptt) {
		return;
	}
	s.ptt = down;
	if (s.hal == NULL) {
		return;
	}
	if (down) {
		if (s.tx != TXL_IDLE) {
			return;
		}
		s.view.tx_error = DMR_TX_OK;
		s.tx_to = effective_target();
		if (!s.running || !s.ready || (caps() & DMRC_CAP_CALLS) == 0 ||
		    !s.hal->call(call_type(s.tx_to), s.tx_to.id, true)) {
			s.view.tx_error = !s.running ? DMR_TX_ERR_NO_MODEM : DMR_TX_ERR_REJECTED;
			s.tx = TXL_DENIED;
			s.stats.tx_denied++;
			dirty();
			return;
		}
		s.stats.tx_calls++;
		memset(&s.view.caller, 0, sizeof(s.view.caller));
		s.view.caller.src = s.cfg.own_id;
		s.view.caller.dst = s.tx_to.id;
		s.view.caller.group = !s.tx_to.private_call;
		s.reply_set = false;	// our own call: the window starts again after it
		// Without state reports, the call counts as on air once the chip took it
		s.tx = (caps() & DMRC_CAP_CALL_INFO) != 0 ? TXL_ASKED : TXL_ON;
		s.tx_start_ms = s.view.call_start_ms = s.now;
	} else {
		if (s.tx == TXL_ASKED || s.tx == TXL_ON) {
			s.hal->call(call_type(s.tx_to), s.tx_to.id, false);
			// Our call ends: replies to its target come back within the window
			uint8_t w = reply_window_s(s.tx_to.private_call);
			if (w != 0) {
				s.reply = s.tx_to;
				s.reply_until = s.now + (uint32_t)w * 1000u;
				s.reply_set = true;
			}
		}
		s.tx = TXL_IDLE;
	}
	dirty();
}

void dmr_call_set_tx_gps(const dmr_gps_t *gps)
{
	(void)gps;
}

void dmr_call_reply_cancel(void)
{
	s.reply_set = false;
	dirty();
}

static void view_update(void)
{
	dmr_call_view_t *v = &s.view;
	uint8_t state, phase;
	switch (s.tx) {
	case TXL_ASKED:
		state = DMR_CALL_STATE_TX;
		phase = DMR_CALL_PHASE_TX_ACCESS;
		break;
	case TXL_ON:
		state = DMR_CALL_STATE_TX;
		phase = DMR_CALL_PHASE_TX_VOICE;
		break;
	case TXL_DENIED:
		state = DMR_CALL_STATE_IDLE;
		phase = DMR_CALL_PHASE_TX_DENIED;
		break;
	default:
		state = s.rx ? DMR_CALL_STATE_RX : DMR_CALL_STATE_IDLE;
		phase = s.rx ? DMR_CALL_PHASE_RX_VOICE : DMR_CALL_PHASE_IDLE;
		break;
	}
	bool reply = reply_running();
	uint8_t reply_s = reply ? (uint8_t)((s.reply_until - s.now + 999u) / 1000u) : 0;
	dmr_target_t target = effective_target();
	uint32_t call_ms = (s.rx || s.tx == TXL_ON) ? s.now - v->call_start_ms : 0;
	if (state != v->state || phase != v->phase || reply != v->reply_active || reply_s != v->reply_s ||
	    !same_target(target, v->tx_target) || call_ms / 1000u != v->call_ms / 1000u) {
		v->serial++;
	}
	v->state = state;
	v->phase = phase;
	v->reply_active = reply;
	v->reply_s = reply_s;
	v->tx_target = target;
	v->call_ms = call_ms;
	v->repeater_up = s.cfg.rmo && s.ready;
}

void dmr_call_poll(uint32_t now_ms)
{
	s.now = now_ms;
	if (s.hal == NULL) {
		return;
	}
	s.hal->poll(now_ms);
	if (s.running && s.ready && !s.pushed) {
		push();		// a refused request (queue full) goes again
	}
	if (s.tx == TXL_ON && s.cfg.tot_s != 0 && now_ms - s.tx_start_ms >= (uint32_t)s.cfg.tot_s * 1000u) {
		s.hal->call(call_type(s.tx_to), s.tx_to.id, false);
		s.tx = s.ptt ? TXL_DENIED : TXL_IDLE;
		s.view.tx_error = DMR_TX_ERR_TIMEOUT;
		s.stats.tx_denied++;
		dirty();
	}
	view_update();
}

void dmr_call_set_wake(void (*fn)(void))
{
	(void)fn;		// the chip's events come from poll; nothing to wake
}

const dmr_call_view_t *dmr_call_view(void)
{
	return &s.view;
}

void dmr_call_get_stats(dmr_call_stats_t *out)
{
	*out = s.stats;
}

void dmr_call_tx_alias(char out[DMR_TA_TEXT_MAX + 1])
{
	out[0] = '\0';
}

void dmr_call_reset_stats(void)
{
	memset(&s.stats, 0, sizeof(s.stats));
}

void dmr_call_set_rx_trace(bool on)
{
	(void)on;
}

bool dmr_call_channel_clear(uint32_t now_ms)
{
	(void)now_ms;
	return !s.rx && s.tx == TXL_IDLE;
}

bool dmr_call_last_activity(uint32_t *ms)
{
	if (!s.activity) {
		return false;
	}
	*ms = s.last_activity_ms;
	return true;
}

void dmr_call_set_data_rx(dmr_data_rx_fn fn, void *ctx)
{
	(void)fn;
	(void)ctx;
}

bool dmr_call_data_tx(const dmr_data_source_t *src)
{
	(void)src;
	return false;		// no burst access on a call-level chip
}

bool dmr_call_data_busy(void)
{
	return false;
}

const dmrcl_info_t *dmrcl_info(void)
{
	s.info.ready = s.ready;
	s.info.running = s.running;
	return &s.info;
}
