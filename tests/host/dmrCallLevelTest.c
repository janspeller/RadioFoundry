// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The call-level DMR backend (core/dmr/call_level, docs/spec/dmr-call-level.md) against a fake chip: the channel,
// groups and key to the chip after its ready report and on each change, the key read from the store (an empty one
// turns encryption off), a call heard into the view and the reply window, PTT to the talk target or refused without call
// support, the TX time-out, and a fault.
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "board.h"
#include "dmr/call/dmr_call.h"
#include "dmr/call_level/dmr_call_level.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- The fake chip ---

static dmrc_event_cb_t cb;
static void *cb_ctx;
static bool powered, ready;
static unsigned n_channel, n_groups, n_key, n_call;
static dmrc_channel_t last_ch;
static uint8_t last_groups;
static bool key_null;
static dmrc_key_t last_key;		// the copy the chip got (a real driver keeps it until sent)
static dmrc_call_type_t call_type;
static uint32_t call_id;
static bool call_on;
static bool calls_ok = true;

static void f_power(bool on) { powered = on; if (!on) ready = false; }
static bool f_ready(void) { return ready; }
static void f_poll(uint32_t now) { (void)now; }
static void f_listen(dmrc_event_cb_t c, void *ctx) { cb = c; cb_ctx = ctx; }
static bool f_channel(const dmrc_channel_t *ch) { n_channel++; last_ch = *ch; return true; }
static bool f_groups(const uint32_t *ids, uint8_t n) { (void)ids; n_groups++; last_groups = n; return true; }
static bool f_call(dmrc_call_type_t t, uint32_t id, bool on)
{
	n_call++;
	call_type = t;
	call_id = id;
	call_on = on;
	return calls_ok;
}
static bool f_key(const dmrc_key_t *k)
{
	n_key++;
	key_null = k == NULL;
	if (k != NULL) {
		last_key = *k;
	}
	return true;
}

// The key store: key 2 is AES-128, key 3 is empty
static bool f_key_get(uint8_t number, dmrc_key_t *out)
{
	if (number != 2) {
		return false;
	}
	out->number = 2;
	out->type = DMRC_KEY_AES128;
	memset(out->bytes, 0xC3, sizeof(out->bytes));
	return true;
}

static hal_dmr_call_t chip = {
	.caps = DMRC_CAP_ENC_AES128,
	.power = f_power, .is_ready = f_ready, .poll = f_poll, .listen = f_listen, .channel = f_channel,
	.groups = f_groups, .call = f_call, .key = f_key, .key_count = 4, .key_get = f_key_get,
};

const board_t board = { .dmr_call = &chip };

static void ev(const dmrc_event_t *e)
{
	cb(e, cb_ctx);
}

static void ev_ready(void)
{
	ready = true;
	dmrc_event_t e = { .type = DMRC_EV_READY, .u.ready.version = 0x0102000Du };
	ev(&e);
}

static void ev_call(bool start, dmrc_call_type_t t, uint32_t src, uint32_t dst)
{
	dmrc_event_t e = { .type = DMRC_EV_CALL };
	e.u.call.start = start;
	e.u.call.call_type = (uint8_t)t;
	e.u.call.src = src;
	e.u.call.dst = dst;
	ev(&e);
}

static uint32_t now = 1000;

static void poll_ms(uint32_t ms)
{
	for (uint32_t i = 0; i < ms; i += 10) {
		now += 10;
		dmr_call_poll(now);
	}
}

static dmr_call_config_t config(void)
{
	dmr_call_config_t c;
	dmr_call_config_default(&c);
	c.own_id = 2620001;
	c.cc = 3;
	c.ts = 2;
	c.talk_target.id = 262;
	c.rx_hz = 438500000u;
	c.tx_hz = 430900000u;
	c.power = 1;
	c.key_number = 2;
	return c;
}

static void test_bring_up(void)
{
	dmr_call_init(NULL);
	dmr_call_config_t c = config();
	static const uint32_t groups[] = { 262, 263, 9 };
	c.listen.groups = groups;
	c.listen.group_count = 3;
	dmr_call_configure(&c);
	CHECK(n_channel == 0);			// nothing before the chip is up
	CHECK(dmr_call_start());
	CHECK(powered && n_channel == 0);
	poll_ms(50);
	ev_ready();
	CHECK(n_channel == 1 && n_groups == 1 && last_groups == 3);
	CHECK(last_ch.rx_hz == 438500000u && last_ch.tx_hz == 430900000u && last_ch.cc == 3 && last_ch.ts == 2);
	CHECK(last_ch.own_id == 2620001 && last_ch.tx_id == 262 && !last_ch.tx_private && last_ch.power == 1);
	CHECK(dmrcl_info()->ready && dmrcl_info()->version == 0x0102000Du);
#if CONFIG_DMR_AES
	CHECK(last_ch.key_number == 2);
	CHECK(n_key == 1 && !key_null && last_key.type == DMRC_KEY_AES128 && last_key.bytes[0] == 0xC3);
	CHECK(dmrcl_info()->key_number == 2 && dmrcl_info()->key_type == DMRC_KEY_AES128 && !dmrcl_info()->key_loaded);
	dmrc_event_t k = { .type = DMRC_EV_KEY, .u.key_ok = true };
	ev(&k);
	CHECK(dmrcl_info()->key_loaded);
	// A channel with an empty key: encryption off
	c.key_number = 3;
	dmr_call_configure(&c);
	CHECK(n_key == 2 && key_null && dmrcl_info()->key_number == 0);
	c.key_number = 2;
	dmr_call_configure(&c);
	CHECK(n_key == 3 && !key_null);
#endif
	// A tx frequency of 0 is the receive one (simplex)
	c.tx_hz = 0;
	dmr_call_configure(&c);
	CHECK(last_ch.tx_hz == 438500000u);
	// The digital monitor (hear all) reaches the chip with every configure, the channel otherwise unchanged
	CHECK(!last_ch.hear_all);
	unsigned n = n_channel;
	c.listen.hear_all = true;
	dmr_call_configure(&c);
	CHECK(n_channel == n + 1 && last_ch.hear_all && last_ch.rx_hz == 438500000u);
	c.listen.hear_all = false;
	dmr_call_configure(&c);
	CHECK(n_channel == n + 2 && !last_ch.hear_all);
}

static void test_call_heard(void)
{
	ev_call(true, DMRC_CALL_GROUP, 2620099, 262);
	poll_ms(20);
	const dmr_call_view_t *v = dmr_call_view();
	CHECK(v->state == DMR_CALL_STATE_RX && v->caller.src == 2620099 && v->caller.dst == 262 && v->caller.group);
	// The chip's signal level is the call's
	dmrc_event_t r = { .type = DMRC_EV_RSSI, .u.rssi_dbm = -87 };
	ev(&r);
	CHECK(v->rssi_dbm == -87);
	ev_call(false, DMRC_CALL_GROUP, 2620099, 262);
	poll_ms(20);
	CHECK(v->state == DMR_CALL_STATE_IDLE && v->last_heard.src == 2620099);
	// A private call to us: the reply window goes back to the caller
	ev_call(true, DMRC_CALL_PRIVATE, 2620077, 2620001);
	poll_ms(20);
	CHECK(v->caller.to_us && !v->caller.group);
	ev_call(false, DMRC_CALL_PRIVATE, 2620077, 2620001);
	poll_ms(20);
	CHECK(v->reply_active && v->tx_target.id == 2620077 && v->tx_target.private_call);
	dmr_call_reply_cancel();
	poll_ms(20);
	CHECK(!v->reply_active && v->tx_target.id == 262);
}

static void test_ptt(void)
{
	const dmr_call_view_t *v = dmr_call_view();
	// The chip can't start calls (no DMRC_CAP_CALLS): refused, nothing sent
	unsigned before = n_call;
	dmr_call_ptt(true);
	poll_ms(20);
	CHECK(n_call == before && v->phase == DMR_CALL_PHASE_TX_DENIED && v->tx_error == DMR_TX_ERR_REJECTED);
	dmr_call_ptt(false);
	poll_ms(20);
	CHECK(v->phase == DMR_CALL_PHASE_IDLE);
	// With calls: a group call to the talk target, and its end
	chip.caps |= DMRC_CAP_CALLS;
	dmr_call_ptt(true);
	poll_ms(20);
	CHECK(n_call == before + 1 && call_on && call_type == DMRC_CALL_GROUP && call_id == 262);
	CHECK(v->state == DMR_CALL_STATE_TX && v->phase == DMR_CALL_PHASE_TX_VOICE);
	dmr_call_ptt(false);
	poll_ms(20);
	CHECK(n_call == before + 2 && !call_on && v->state == DMR_CALL_STATE_IDLE);
	// The TX time-out ends the call
	dmr_call_config_t c = config();
	c.tot_s = 2;
	dmr_call_configure(&c);
	dmr_call_ptt(true);
	poll_ms(2100);
	CHECK(!call_on && v->tx_error == DMR_TX_ERR_TIMEOUT && v->phase == DMR_CALL_PHASE_TX_DENIED);
	dmr_call_ptt(false);
	// The chip refuses the start
	calls_ok = false;
	dmr_call_ptt(true);
	poll_ms(20);
	CHECK(v->tx_error == DMR_TX_ERR_REJECTED);
	dmr_call_ptt(false);
	calls_ok = true;
	// A chip that reports the TX state (DMRC_CAP_CALL_INFO): on the air only once it says so, refused when it says no
	chip.caps |= DMRC_CAP_CALL_INFO;
	c.tot_s = 0;
	dmr_call_configure(&c);
	dmr_call_ptt(true);
	poll_ms(20);
	CHECK(call_on && v->state == DMR_CALL_STATE_TX && v->phase == DMR_CALL_PHASE_TX_ACCESS);
	dmrc_event_t st = { .type = DMRC_EV_STATE, .u.state = DMRC_ST_TX };
	ev(&st);
	poll_ms(20);
	CHECK(v->phase == DMR_CALL_PHASE_TX_VOICE);
	dmr_call_ptt(false);
	poll_ms(20);
	CHECK(!call_on && v->state == DMR_CALL_STATE_IDLE);
	dmr_call_ptt(true);
	poll_ms(20);
	st.u.state = DMRC_ST_TX_REFUSED;
	ev(&st);
	poll_ms(20);
	CHECK(v->phase == DMR_CALL_PHASE_TX_DENIED && v->tx_error == DMR_TX_ERR_BUSY);
	before = n_call;
	dmr_call_ptt(false);
	poll_ms(20);
	CHECK(n_call == before && v->phase == DMR_CALL_PHASE_IDLE);	// refused: the driver ends it itself
	// All call: the all call ID as the talk target, with DMRC_CAP_ALL_CALL
	chip.caps |= DMRC_CAP_ALL_CALL;
	c.talk_target.id = DMR_ID_ALL_CALL;
	dmr_call_configure(&c);
	dmr_call_ptt(true);
	poll_ms(20);
	CHECK(call_on && call_type == DMRC_CALL_ALL);
	dmr_call_ptt(false);
	poll_ms(20);
	chip.caps &= ~(DMRC_CAP_CALLS | DMRC_CAP_CALL_INFO | DMRC_CAP_ALL_CALL);
	c = config();
	dmr_call_configure(&c);
}

static void test_fault_and_stop(void)
{
	dmrc_event_t f = { .type = DMRC_EV_FAULT, .u.fault.reason = 2 };
	ev(&f);
	CHECK(!dmrcl_info()->ready && dmrcl_info()->fault == 2 && dmrcl_info()->faults == 1);
	unsigned before = n_channel;
	ev_ready();
	CHECK(n_channel == before + 1 && dmrcl_info()->fault == 0);
	dmr_call_stop();
	CHECK(!powered && !dmrcl_info()->running);
	// Data and the burst-level extras don't exist here
	CHECK(!dmr_call_data_tx(NULL) && !dmr_call_data_busy());
}

int main(void)
{
	test_bring_up();
	test_call_heard();
	test_ptt();
	test_fault_and_stop();
	if (failures != 0) {
		printf("dmrCallLevelTest: %d failures\n", failures);
		return 1;
	}
	printf("dmrCallLevelTest: all passed\n");
	return 0;
}
