// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The HF6853 driver (drivers/dmr/hf6853, docs/spec/hardware-rt4d.md 5): the frame layer (checksum, build, parse), the
// command data of 5.4.1, and the engine against a fake chip: the power-up wait and its retry, the stock start-up,
// pacing with one request in flight, retries and give-ups, the channel change in the stock order with 0x62 (hear all),
// the key cleared once sent, calls both ways (5.4.2) with a call heard ended by 0x6F or the long fallback and the
// speaker following it, the signal level (0x59, 0x05, 5.4.3) never holding a PTT, and no key bytes in the debug link
// lines.
#include <stdio.h>
#include <string.h>
#include "dmr/hf6853/hf6853.h"
#include "dmr/hf6853/hf6853_frame.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- The fake chip ---

#define LOG_MAX		512	// a call heard 30 s long has its level asked for 60 times
typedef struct {
	uint8_t f[HF6853_TX_MAX];
	size_t len;
	uint32_t ms;
} sent_t;

static sent_t sent[LOG_MAX];
static unsigned n_sent;
static bool cen;
static unsigned cen_ups, cen_downs;
static uint32_t now;
static uint8_t inq[512];
static size_t in_head, in_tail;
static bool answer = true;		// the chip replies to every request
static uint8_t mute_cmd;		// ... except to this command (0 none)
static uint8_t status_for_cmd, status_value;	// a reply status other than 0 for one command
static unsigned status_times;
static unsigned in_flight_violations;	// a request sent while an unanswered one could still get its reply
static bool muted_pending;
static uint32_t muted_at;
static uint8_t muted_sent;
static uint8_t rssi_level = 40;		// the raw level the chip answers 0x05 with
static bool audio_on;			// the speaker hook's level
static unsigned audio_changes;

static size_t mk(uint8_t *f, uint8_t cmd, uint8_t rw, uint8_t sr, const uint8_t *data, uint16_t len, bool check)
{
	if (len != 0) {
		memcpy(f + HF6853_HDR, data, len);
	}
	size_t n = hf6853_frame_finish(f, cmd, rw, len, false);
	f[3] = sr;
	if (check) {
		uint16_t c = hf6853_checksum(f, n);
		f[4] = (uint8_t)(c >> 8);
		f[5] = (uint8_t)c;
	}
	return n;
}

static void chip_send(const uint8_t *f, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		inq[in_head++ % sizeof(inq)] = f[i];
	}
}

static void chip_report(uint8_t cmd, uint8_t sr, const uint8_t *data, uint16_t len)
{
	uint8_t f[80];
	chip_send(f, mk(f, cmd, HF6853_RW_REPORT, sr, data, len, true));
}

static void io_enable(bool on)
{
	if (on && !cen) {
		cen_ups++;
	}
	if (!on && cen) {
		cen_downs++;
	}
	cen = on;
}

static size_t io_read(uint8_t *buf, size_t max)
{
	size_t n = 0;
	while (n < max && in_tail != in_head) {
		buf[n++] = inq[in_tail++ % sizeof(inq)];
	}
	return n;
}

static bool io_write(uint8_t *frame, size_t len)
{
	if (muted_pending && now - muted_at < hf6853_reply_ms(muted_sent)) {
		in_flight_violations++;
	}
	muted_pending = false;
	if (n_sent < LOG_MAX) {
		memcpy(sent[n_sent].f, frame, len);
		sent[n_sent].len = len;
		sent[n_sent].ms = now;
		n_sent++;
	}
	uint8_t cmd = frame[1];
	memset(frame, 0, len);		// as the UART does once each byte is out
	if (!answer || cmd == mute_cmd) {
		muted_pending = true;
		muted_at = now;
		muted_sent = cmd;
		return true;
	}
	uint8_t sr = 0;
	if (cmd == status_for_cmd && status_times != 0) {
		status_times--;
		sr = status_value;
	}
	uint8_t f[32];
	static const uint8_t version[] = { 0x01, 0x02, 0x00, 0x0D };
	if (cmd == HF6853_CMD_RSSI) {
		chip_send(f, mk(f, cmd, HF6853_RW_WRITE, sr, &rssi_level, 1, true));
		return true;
	}
	bool v = cmd == HF6853_CMD_VERSION;
	chip_send(f, mk(f, cmd, HF6853_RW_WRITE, sr, v ? version : NULL, v ? sizeof(version) : 0, true));
	return true;
}

static bool io_busy(void)
{
	return false;
}

static void io_audio(bool on)
{
	CHECK(on != audio_on);		// changes only
	audio_on = on;
	audio_changes++;
}

static const hf6853_io_t io = { io_enable, io_read, io_write, io_busy, io_audio };

// --- Events ---

static dmrc_event_t evs[128];
static unsigned n_ev;

static void on_event(const dmrc_event_t *ev, void *ctx)
{
	(void)ctx;
	if (n_ev < 128) {
		evs[n_ev++] = *ev;
	}
}

static unsigned count_ev(uint8_t type)
{
	unsigned n = 0;
	for (unsigned i = 0; i < n_ev; i++) {
		n += evs[i].type == type;
	}
	return n;
}

static void run(uint32_t ms)
{
	for (uint32_t i = 0; i < ms; i += 10) {
		now += 10;
		hf6853_poll(now);
	}
}

static void reset(void)
{
	memset(sent, 0, sizeof(sent));
	n_sent = 0;
	cen = false;
	cen_ups = cen_downs = 0;
	in_head = in_tail = 0;
	answer = true;
	mute_cmd = 0;
	status_for_cmd = status_value = 0;
	status_times = 0;
	in_flight_violations = 0;
	muted_pending = false;
	rssi_level = 40;
	audio_on = false;
	audio_changes = 0;
	n_ev = 0;
	now = 1000;
	hf6853_init(&io);
	hf6853_listen(on_event, NULL);
}

static bool frame_ok(const sent_t *s)
{
	uint16_t c = (uint16_t)(s->f[4] << 8 | s->f[5]);
	return s->f[0] == HF6853_HEAD && s->f[s->len - 1] == HF6853_TAIL &&
	       s->len == HF6853_OVERHEAD + (size_t)(s->f[6] << 8 | s->f[7]) && (c == 0 || c == hf6853_checksum(s->f, s->len));
}

// Up to the ready event: power on, the 0x1A report, the start-up
static void bring_up(void)
{
	hf6853_power(true);
	run(200);
	chip_report(HF6853_CMD_READY, 0, NULL, 0);
	run(3000);
}

// --- Tests ---

static void test_checksum(void)
{
	// 68 0C 01 01 cs cs 00 01 00 10: 0x680C + 0x0101 + 0x0001 + 0x0010 = 0x691E, inverted 0x96E1
	uint8_t f[16];
	uint8_t zero = 0;
	memcpy(f + HF6853_HDR, &zero, 1);
	size_t n = hf6853_frame_finish(f, 0x0C, 0x01, 1, true);
	CHECK(n == 10);
	CHECK(f[0] == 0x68 && f[1] == 0x0C && f[2] == 0x01 && f[3] == 0x01 && f[6] == 0 && f[7] == 1 && f[9] == 0x10);
	CHECK(f[4] == 0x96 && f[5] == 0xE1);
	// The field taken as zero: the same sum whatever it holds
	CHECK(hf6853_checksum(f, n) == 0x96E1);
	// The whole frame then sums to 0xFFFF (one's complement)
	uint32_t s = 0;
	for (size_t i = 0; i < n; i += 2) {
		s += (uint32_t)f[i] << 8 | (i + 1 < n ? f[i + 1] : 0);
	}
	while (s >> 16) {
		s = (s & 0xFFFF) + (s >> 16);
	}
	CHECK(s == 0xFFFF);
	// An odd length: the tail is a word's high byte. 68 25 00 01 cs cs 00 00 10: 0x6825 + 0x0001 + 0x0000 + 0x1000
	n = hf6853_frame_finish(f, 0x25, 0x00, 0, true);
	CHECK(n == 9);
	CHECK(hf6853_checksum(f, n) == (uint16_t)~(0x6825 + 0x0001 + 0x1000));
	// No check: 0x0000
	n = hf6853_frame_finish(f, 0x42, 0x01, 0, false);
	CHECK(f[4] == 0 && f[5] == 0);
}

static void test_parse(void)
{
	hf6853_rx_t rx;
	memset(&rx, 0, sizeof(rx));
	uint8_t f[80];
	static const uint8_t data[] = { 1, 2, 3 };
	size_t n = mk(f, 0x04, HF6853_RW_REPORT, 0, data, 3, true);
	// Noise first, then the frame
	CHECK(!hf6853_rx_byte(&rx, 0x00));
	CHECK(!hf6853_rx_byte(&rx, 0x10));
	bool got = false;
	for (size_t i = 0; i < n; i++) {
		got = hf6853_rx_byte(&rx, f[i]);
		CHECK(got == (i == n - 1));
	}
	CHECK(got && hf6853_rx_cmd(&rx) == 0x04 && hf6853_rx_rw(&rx) == 2 && hf6853_rx_len(&rx) == 3);
	CHECK(memcmp(hf6853_rx_data(&rx), data, 3) == 0);
	// A broken checksum
	f[8] ^= 1;
	got = false;
	for (size_t i = 0; i < n; i++) {
		got |= hf6853_rx_byte(&rx, f[i]);
	}
	CHECK(!got && rx.bad == 1);
	f[8] ^= 1;
	// A wrong tail
	f[n - 1] = 0x11;
	got = false;
	for (size_t i = 0; i < n; i++) {
		got |= hf6853_rx_byte(&rx, f[i]);
	}
	CHECK(!got && rx.bad == 2);
	// Checksum 0x0000: taken, counted
	n = mk(f, 0x1A, HF6853_RW_REPORT, 0, NULL, 0, false);
	got = false;
	for (size_t i = 0; i < n; i++) {
		got |= hf6853_rx_byte(&rx, f[i]);
	}
	CHECK(got && rx.unchecked == 1 && hf6853_rx_cmd(&rx) == 0x1A);
	// Too long to keep: skipped whole, and the next frame still parses
	uint8_t big[HF6853_RX_DATA_MAX + 20];
	memset(big, 0x68, sizeof(big));		// heads inside the data must not resync
	uint8_t bf[sizeof(big) + HF6853_OVERHEAD];
	size_t bn = mk(bf, 0x11, HF6853_RW_REPORT, 0, big, sizeof(big), true);
	got = false;
	for (size_t i = 0; i < bn; i++) {
		got |= hf6853_rx_byte(&rx, bf[i]);
	}
	CHECK(!got && rx.too_long == 1);
	n = mk(f, 0x05, HF6853_RW_REPORT, 0, data, 1, true);
	got = false;
	for (size_t i = 0; i < n; i++) {
		got |= hf6853_rx_byte(&rx, f[i]);
	}
	CHECK(got && hf6853_rx_cmd(&rx) == 0x05);
}

static void test_commands(void)
{
	uint8_t out[HF6853_KEY_DATA_MAX];
	dmrc_channel_t ch = { .rx_hz = 438500000u, .tx_hz = 430900000u, .cc = 7, .ts = 2, .tx_id = 262, .key_number = 3 };
	CHECK(hf6853_cmd_channel(out, &ch) == 20);
	static const uint8_t want[20] = { 2, 7, 0, 0x1A, 0x22, 0xFA, 0xA0, 0x19, 0xAF, 0x03, 0x20, 4, 0xAA, 0xAA, 0xAA, 0xAA,
					   0, 0, 0, 3 };
	CHECK(memcmp(out, want, 20) == 0);
	ch.ts = 1;
	ch.cc = 0x1F;
	hf6853_cmd_channel(out, &ch);
	CHECK(out[0] == 1 && out[1] == 0x0F);

	dmrc_key_t k = { .number = 2, .type = DMRC_KEY_AES256 };
	for (unsigned i = 0; i < 32; i++) {
		k.bytes[i] = (uint8_t)(0xA0 + i);
	}
	CHECK(hf6853_cmd_key(out, &k) == 34 && out[0] == 2 && out[1] == 5 && out[2] == 0xA0 && out[33] == 0xBF);
	k.type = DMRC_KEY_AES128;
	CHECK(hf6853_cmd_key(out, &k) == 18 && out[1] == 4 && out[17] == 0xAF);
	k.type = DMRC_KEY_ARC;
	CHECK(hf6853_cmd_key(out, &k) == 7 && out[1] == 1 && out[6] == 0xA4);
	k.type = DMRC_KEY_OFF;
	CHECK(hf6853_cmd_key(out, &k) == 2 && out[0] == 0 && out[1] == 0);
	CHECK(hf6853_cmd_key(out, NULL) == 2 && out[0] == 0 && out[1] == 0);
	k.type = DMRC_KEY_AES128;
	k.number = 0;
	CHECK(hf6853_cmd_key(out, &k) == 2 && out[0] == 0);

	CHECK(hf6853_cmd_own_id(out, 1234567u) == 4 && out[0] == 0x67 && out[1] == 0x45 && out[2] == 0x23 && out[3] == 0x01);
	// 0x05's raw level on the placeholder line: the stock floor at -120 dBm, its full bar at -60, within -160 .. 0
	CHECK(hf6853_rssi_dbm(21) == -120 && hf6853_rssi_dbm(57) == -60 && hf6853_rssi_dbm(39) == -90);
	CHECK(hf6853_rssi_dbm(0) == -155 && hf6853_rssi_dbm(255) == 0);
	CHECK(hf6853_reply_ms(0x06) == 200 && hf6853_reply_ms(0x07) == 4000 && hf6853_reply_ms(0x48) == 2000 &&
	      hf6853_reply_ms(0x82) == 100);
}

static void test_power_up_retry(void)
{
	reset();
	CHECK(!cen);
	hf6853_power(true);
	CHECK(cen && cen_ups == 1);
	run(2900);
	CHECK(cen && count_ev(DMRC_EV_FAULT) == 0);
	run(200);
	// No 0x1A within 3 s: a fault, powered off for 3 s, then on again
	CHECK(!cen && count_ev(DMRC_EV_FAULT) == 1 && evs[0].u.fault.reason == HF6853_FAULT_NO_READY);
	run(2900);
	CHECK(!cen);
	run(200);
	CHECK(cen && cen_ups == 2);
	CHECK(n_sent == 0);		// nothing goes to a chip that isn't up
	// A 0x1A with a status other than 0 isn't ready
	chip_report(HF6853_CMD_READY, 1, NULL, 0);
	run(1000);
	CHECK(n_sent == 0 && !hf6853_is_ready());
	// The status in the first DATA byte also counts (which field holds it is open)
	static const uint8_t ok = 0;
	chip_report(HF6853_CMD_READY, 1, &ok, 1);
	run(3000);
	CHECK(hf6853_is_ready() && count_ev(DMRC_EV_READY) == 1);
	hf6853_power(false);
	CHECK(!cen && !hf6853_is_ready());
}

static void test_startup(void)
{
	reset();
	bring_up();
	CHECK(hf6853_is_ready());
	// The stock start-up of 5.5, then the version, all with checksum 0x0000
	static const uint8_t order[] = { 0x42, 0x3E, 0x0C, 0x45, 0x48, 0x4C, 0x25 };
	CHECK(n_sent == sizeof(order));
	for (unsigned i = 0; i < n_sent && i < sizeof(order); i++) {
		CHECK(sent[i].f[1] == order[i]);
		CHECK(frame_ok(&sent[i]) && sent[i].f[4] == 0 && sent[i].f[5] == 0);
		if (i > 0) {
			CHECK(sent[i].ms - sent[i - 1].ms >= 100);
		}
	}
	CHECK(sent[0].f[7] == 1 && sent[0].f[8] == 0xFF);
	CHECK(sent[1].f[7] == 15 && sent[1].f[8] == 0x0F && sent[1].f[22] == 0x0F);
	CHECK(sent[2].f[8] == 0x00 && sent[3].f[8] == 0x02 && sent[4].f[8] == 0x0F && sent[5].f[8] == 0x01);
	CHECK(sent[6].f[2] == HF6853_RW_READ && sent[6].f[7] == 0);
	// The waits: 500 ms after 0x1A, 500 ms after 0x42, 100 ms after 0x3E
	CHECK(sent[1].ms - sent[0].ms >= 500 && sent[2].ms - sent[1].ms >= 100);
	CHECK(count_ev(DMRC_EV_READY) == 1 && evs[0].u.ready.version == 0x0102000Du);
	CHECK(in_flight_violations == 0);
	// The chip restarts on its own: its start-up goes again, and ready once more
	chip_report(HF6853_CMD_READY, 0, NULL, 0);
	run(10);
	CHECK(!hf6853_is_ready());
	run(3000);
	CHECK(hf6853_is_ready() && count_ev(DMRC_EV_READY) == 2 && n_sent == 2 * sizeof(order));
}

static bool view_has(const char *needle);
static const sent_t *last_sent(uint8_t cmd);
static unsigned count_sent(unsigned from, uint8_t cmd);

static void test_channel_change(void)
{
	reset();
	bring_up();
	unsigned base = n_sent;
	dmrc_channel_t ch = { .rx_hz = 438500000u, .tx_hz = 430900000u, .cc = 1, .ts = 1, .own_id = 2620001u, .tx_id = 91,
			      .key_number = 1 };
	dmrc_key_t k = { .number = 1, .type = DMRC_KEY_AES128 };
	memset(k.bytes, 0x5A, 16);
	CHECK(hf6853_channel(&ch));
	CHECK(hf6853_groups(NULL, 0));
	CHECK(hf6853_key(&k));
	run(1000);
	// The stock order: 0x2A, 0x82, 0x81, 0x62, 0x0C (the group commands wait for their formats)
	CHECK(n_sent == base + 5);
	static const uint8_t order[] = { 0x2A, 0x82, 0x81, 0x62, 0x0C };
	for (unsigned i = 0; i < 5; i++) {
		const sent_t *s = &sent[base + i];
		CHECK(s->f[1] == order[i] && s->f[2] == HF6853_RW_WRITE && s->f[3] == 0x01);
		CHECK(frame_ok(s) && (s->f[4] | s->f[5]) != 0);		// checked frames
		CHECK(i == 0 || s->ms - sent[base + i - 1].ms >= 100);
	}
	// 0x2A: our own ID in little-endian BCD, never the talk target
	CHECK(sent[base].f[7] == 4 && sent[base].f[8] == 0x01 && sent[base].f[9] == 0x00 && sent[base].f[10] == 0x62 &&
	      sent[base].f[11] == 0x02);
	CHECK(sent[base + 1].f[7] == 20 && sent[base + 1].f[8 + 19] == 1);
	const sent_t *kf = &sent[base + 2];
	CHECK(kf->f[7] == 18 && kf->f[8] == 1 && kf->f[9] == 4 && kf->f[10] == 0x5A && kf->f[25] == 0x5A);
	// 0x62: 1, then hear all off
	CHECK(sent[base + 3].f[7] == 2 && sent[base + 3].f[8] == 1 && sent[base + 3].f[9] == 0);
	CHECK(sent[base + 4].f[7] == 1 && sent[base + 4].f[8] == 0);
	CHECK(count_ev(DMRC_EV_KEY) == 1 && evs[n_ev - 1].u.key_ok);
	CHECK(in_flight_violations == 0);
	CHECK(view_has("hear_all=0"));
	// The same channel again (the core sends it on every change of its configuration): nothing goes out
	base = n_sent;
	CHECK(hf6853_channel(&ch));
	run(500);
	CHECK(n_sent == base);
	// hear all on (the digital monitor): 0x62 alone, 1 1
	ch.hear_all = true;
	CHECK(hf6853_channel(&ch));
	run(500);
	CHECK(n_sent == base + 1 && sent[base].f[1] == 0x62 && sent[base].f[7] == 2 && sent[base].f[8] == 1 &&
	      sent[base].f[9] == 1 && frame_ok(&sent[base]));
	CHECK(view_has("hear_all=1"));
	// Off again before the 0x62 for on was answered: the off follows it
	mute_cmd = 0x62;
	base = n_sent;
	ch.hear_all = false;
	CHECK(hf6853_channel(&ch));
	run(20);
	ch.hear_all = true;
	CHECK(hf6853_channel(&ch));
	mute_cmd = 0;
	run(1000);
	CHECK(count_sent(base, 0x62) >= 2 && last_sent(0x62)->f[9] == 1 && view_has("hear_all=1"));
	// Another channel with hear all on: the whole sequence, 0x62 saying on
	base = n_sent;
	ch.rx_hz = 439000000u;
	CHECK(hf6853_channel(&ch));
	run(1000);
	CHECK(n_sent == base + 4 && sent[base].f[1] == 0x2A && sent[base + 1].f[1] == 0x82 && sent[base + 2].f[1] == 0x62 &&
	      sent[base + 2].f[9] == 1 && sent[base + 3].f[1] == 0x0C);
	CHECK(in_flight_violations == 0);
	// Encryption off
	CHECK(hf6853_key(NULL));
	run(300);
	CHECK(sent[n_sent - 1].f[1] == 0x81 && sent[n_sent - 1].f[7] == 2 && sent[n_sent - 1].f[8] == 0 &&
	      sent[n_sent - 1].f[9] == 0);
	// The chip refuses the key: the event says so
	status_for_cmd = 0x81;
	status_value = 0x02;
	status_times = 1;
	CHECK(hf6853_key(&k));
	run(300);
	CHECK(!evs[n_ev - 1].u.key_ok && evs[n_ev - 1].type == DMRC_EV_KEY);
}

static void test_retries(void)
{
	reset();
	bring_up();
	dmrc_channel_t ch = { .rx_hz = 438500000u, .tx_hz = 438500000u, .cc = 1, .ts = 1, .tx_id = 9 };
	// 0x82 is never answered: sent 6 times, 100 ms apart, then given up, and the change goes on with 0x0C
	unsigned base = n_sent;
	mute_cmd = 0x82;
	hf6853_channel(&ch);
	run(2000);
	unsigned n82 = 0;
	for (unsigned i = base; i < n_sent; i++) {
		n82 += sent[i].f[1] == 0x82;
	}
	CHECK(n82 == 6);
	CHECK(sent[n_sent - 1].f[1] == 0x0C);
	CHECK(in_flight_violations == 0);
	CHECK(count_ev(DMRC_EV_FAULT) == 0);
	// Busy twice, then done: three sends
	mute_cmd = 0;
	base = n_sent;
	status_for_cmd = 0x2A;
	status_value = HF6853_SR_BUSY;
	status_times = 2;
	ch.own_id = 1;
	hf6853_channel(&ch);
	run(1000);
	unsigned n2a = 0;
	for (unsigned i = base; i < n_sent; i++) {
		n2a += sent[i].f[1] == 0x2A;
	}
	CHECK(n2a == 3);
	// The chip stops answering: three requests given up in a row, a fault, power off, and the power-up again
	answer = false;
	ch.own_id = 2;
	hf6853_channel(&ch);
	run(2500);
	CHECK(count_ev(DMRC_EV_FAULT) == 1);
	for (unsigned i = 0; i < n_ev; i++) {
		if (evs[i].type == DMRC_EV_FAULT) {
			CHECK(evs[i].u.fault.reason == HF6853_FAULT_LOST);
		}
	}
	CHECK(cen_downs == 1 && !hf6853_is_ready());
	run(3100);
	CHECK(cen && cen_ups == 2);
}

static void test_call_data(void)
{
	// 0x06 (5.4.2): the type, then the ID as 8 packed BCD digits
	uint8_t c[HF6853_CALL_LEN];
	CHECK(hf6853_cmd_call(c, DMRC_CALL_GROUP, 1234567u) == 5);
	CHECK(c[0] == 2 && c[1] == 0x01 && c[2] == 0x23 && c[3] == 0x45 && c[4] == 0x67);
	hf6853_cmd_call(c, DMRC_CALL_PRIVATE, 91u);
	CHECK(c[0] == 1 && c[1] == 0 && c[2] == 0 && c[3] == 0 && c[4] == 0x91);
	hf6853_cmd_call(c, DMRC_CALL_GROUP, 16777215u);
	CHECK(c[1] == 0x16 && c[2] == 0x77 && c[3] == 0x72 && c[4] == 0x15);
	hf6853_cmd_call(c, DMRC_CALL_ALL, 0);
	CHECK(c[0] == 4 && c[1] == 0xAA && c[2] == 0xAA && c[3] == 0xAA && c[4] == 0xAA);
	// The 0x60 event: type, called ID, caller ID, BCD
	uint32_t not_bcd = 0;
	hf6853_heard_t h;
	static const uint8_t grp[] = { 2, 0x00, 0x00, 0x00, 0x09, 0x01, 0x23, 0x45, 0x67 };
	CHECK(hf6853_parse_heard(grp, sizeof(grp), &h, &not_bcd));
	CHECK(h.type == DMRC_CALL_GROUP && h.dst == 9 && h.src == 1234567u && not_bcd == 0);
	static const uint8_t prv[] = { 1, 0x00, 0x23, 0x45, 0x67, 0x00, 0x00, 0x00, 0x5B };
	CHECK(hf6853_parse_heard(prv, sizeof(prv), &h, &not_bcd));
	CHECK(h.type == DMRC_CALL_PRIVATE && h.dst == 234567u && h.src == 0x5Bu && not_bcd == 1);	// not BCD: raw
	static const uint8_t all[] = { 4, 0xAA, 0xAA, 0xAA, 0xAA, 0x00, 0x00, 0x12, 0x34 };
	CHECK(hf6853_parse_heard(all, sizeof(all), &h, &not_bcd));
	CHECK(h.type == DMRC_CALL_ALL && h.dst == DMRC_ID_ALL && h.src == 1234u && not_bcd == 1);
	CHECK(!hf6853_parse_heard(grp, 8, &h, &not_bcd));
}

static const sent_t *last_sent(uint8_t cmd)
{
	for (unsigned i = n_sent; i-- > 0;) {
		if (sent[i].f[1] == cmd) {
			return &sent[i];
		}
	}
	return NULL;
}

static unsigned count_sent(unsigned from, uint8_t cmd)
{
	unsigned n = 0;
	for (unsigned i = from; i < n_sent; i++) {
		n += sent[i].f[1] == cmd;
	}
	return n;
}

static bool last_state(uint8_t state)
{
	for (unsigned i = n_ev; i-- > 0;) {
		if (evs[i].type == DMRC_EV_STATE) {
			return evs[i].u.state == state;
		}
	}
	return false;
}

static void test_calls(void)
{
	reset();
	CHECK((HF6853_CAPS & (DMRC_CAP_CALLS | DMRC_CAP_CALL_INFO | DMRC_CAP_ALL_CALL)) ==
	      (DMRC_CAP_CALLS | DMRC_CAP_CALL_INFO | DMRC_CAP_ALL_CALL));
	CHECK(!hf6853_call(DMRC_CALL_GROUP, 91, true));		// not up
	bring_up();
	dmrc_channel_t ch = { .rx_hz = 438500000u, .tx_hz = 438500000u, .cc = 1, .ts = 1, .tx_id = 91 };
	hf6853_channel(&ch);
	run(1000);
	// PTT down: 0x06, S/R 0x01, LEN 5, a checked frame; the reply is the TX state
	unsigned base = n_sent;
	CHECK(hf6853_call(DMRC_CALL_GROUP, 91, true));
	run(300);
	const sent_t *s = last_sent(HF6853_CMD_CALL);
	CHECK(s != NULL && count_sent(base, HF6853_CMD_CALL) == 1);
	CHECK(s->f[2] == HF6853_RW_WRITE && s->f[3] == 0x01 && s->f[7] == 5 && frame_ok(s) && (s->f[4] | s->f[5]) != 0);
	CHECK(s->f[8] == 2 && s->f[9] == 0 && s->f[10] == 0 && s->f[11] == 0 && s->f[12] == 0x91);
	CHECK(last_state(DMRC_ST_TX));
	CHECK(view_has("on_air=1"));
	CHECK(!hf6853_call(DMRC_CALL_GROUP, 91, true));		// one call at a time
	// PTT up: the same frame with S/R 0xFF
	CHECK(hf6853_call(DMRC_CALL_GROUP, 91, false));
	run(300);
	s = last_sent(HF6853_CMD_CALL);
	CHECK(count_sent(base, HF6853_CMD_CALL) == 2 && s->f[3] == 0xFF && s->f[7] == 5 && s->f[12] == 0x91 && frame_ok(s));
	CHECK(last_state(DMRC_ST_IDLE) && view_has("on_air=0"));
	// PTT down during a channel change goes after it
	base = n_sent;
	ch.rx_hz = 438600000u;
	hf6853_channel(&ch);
	CHECK(hf6853_call(DMRC_CALL_PRIVATE, 1234567u, true));
	run(1000);
	CHECK(n_sent == base + 5 && sent[base + 4].f[1] == HF6853_CMD_CALL && sent[base + 4].f[8] == 1 &&
	      sent[base + 4].f[9] == 0x01 && sent[base + 4].f[12] == 0x67);
	// PTT up goes ahead of a channel change queued meanwhile
	base = n_sent;
	ch.rx_hz = 438700000u;
	hf6853_channel(&ch);
	hf6853_call(DMRC_CALL_PRIVATE, 1234567u, false);
	run(1000);
	CHECK(sent[base].f[1] == HF6853_CMD_CALL && sent[base].f[3] == 0xFF);
	// PTT up before PTT down went out: nothing sent
	base = n_sent;
	CHECK(hf6853_call(DMRC_CALL_GROUP, 9, true));
	CHECK(hf6853_call(DMRC_CALL_GROUP, 9, false));
	run(500);
	CHECK(count_sent(base, HF6853_CMD_CALL) == 0);
	// All call
	hf6853_call(DMRC_CALL_ALL, DMRC_ID_ALL, true);
	run(300);
	s = last_sent(HF6853_CMD_CALL);
	CHECK(s->f[8] == 4 && s->f[9] == 0xAA && s->f[12] == 0xAA);
	hf6853_call(DMRC_CALL_ALL, DMRC_ID_ALL, false);
	run(300);
	// Any 0x06 reply takes a PTT down, as on stock (5.4.2): an unknown S/R, or a call event; the S/R shows
	status_for_cmd = HF6853_CMD_CALL;
	status_value = 0x05;
	status_times = 1;
	n_ev = 0;
	CHECK(hf6853_call(DMRC_CALL_GROUP, 91, true));
	run(300);
	CHECK(last_state(DMRC_ST_TX) && view_has("on_air=1") && view_has("last_reply=06/05") && view_has("sr06=05"));
	hf6853_call(DMRC_CALL_GROUP, 91, false);
	run(300);
	CHECK(last_state(DMRC_ST_IDLE) && view_has("last_reply=06/00") && view_has("sr06=00,05"));
	status_value = 0x75;
	status_times = 1;
	hf6853_call(DMRC_CALL_GROUP, 91, true);
	run(300);
	CHECK(last_state(DMRC_ST_TX));
	hf6853_call(DMRC_CALL_GROUP, 91, false);
	run(300);
	// The chip refuses the call ("no channel"): TX refused, and a PTT up all the same
	base = n_sent;
	status_for_cmd = HF6853_CMD_CALL;
	status_value = HF6853_SR_NO_CHANNEL;
	status_times = 1;
	hf6853_call(DMRC_CALL_GROUP, 91, true);
	run(500);
	CHECK(count_sent(base, HF6853_CMD_CALL) == 2 && sent[n_sent - 1].f[3] == 0xFF);
	bool refused = false;
	for (unsigned i = 0; i < n_ev; i++) {
		refused = refused || (evs[i].type == DMRC_EV_STATE && evs[i].u.state == DMRC_ST_TX_REFUSED);
	}
	CHECK(refused && view_has("on_air=0"));
	// A PTT up the chip never answers: six tries, then the chip is powered off so it can't stay on the air
	CHECK(hf6853_call(DMRC_CALL_GROUP, 91, true));
	run(300);
	n_ev = 0;
	mute_cmd = HF6853_CMD_CALL;
	base = n_sent;
	unsigned downs = cen_downs;
	hf6853_call(DMRC_CALL_GROUP, 91, false);
	run(3000);
	CHECK(count_sent(base, HF6853_CMD_CALL) == 6);
	CHECK(cen_downs == downs + 1 && !hf6853_is_ready());
	CHECK(count_ev(DMRC_EV_FAULT) == 1 && evs[n_ev - 1].u.fault.reason == HF6853_FAULT_NO_PTT_UP);
	CHECK(in_flight_violations == 0);
}

static unsigned call_events(bool start)
{
	unsigned n = 0;
	for (unsigned i = 0; i < n_ev; i++) {
		n += evs[i].type == DMRC_EV_CALL && evs[i].u.call.start == start;
	}
	return n;
}

static const dmrc_event_t *last_call(void)
{
	static const dmrc_event_t none;
	for (unsigned i = n_ev; i-- > 0;) {
		if (evs[i].type == DMRC_EV_CALL) {
			return &evs[i];
		}
	}
	return &none;
}

static void test_heard(void)
{
	reset();
	bring_up();
	n_ev = 0;
	static const uint8_t a[] = { 2, 0x00, 0x00, 0x00, 0x09, 0x00, 0x23, 0x45, 0x67 };
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, a, sizeof(a));
	run(20);
	CHECK(call_events(true) == 1 && call_events(false) == 0);
	const dmrc_event_t *e = last_call();
	CHECK(e->u.call.call_type == DMRC_CALL_GROUP && e->u.call.dst == 9 && e->u.call.src == 234567u);
	CHECK(view_has("heard=1") && view_has("heard_raw=02:00000009:00234567"));
	CHECK(!view_has("last_report=06"));	// a known event, not an unknown report
	CHECK(audio_on && audio_changes == 1);	// the speaker opens for it
	// The same call again keeps it going. Stock ends a call at 0x6F alone (5.4.2): 2 s and more without an event
	// don't end it, only the long fallback does, should the 0x6F be lost
	CHECK(HF6853_HEARD_QUIET_MS >= 20000u);
	run(1000);
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, a, sizeof(a));
	run(5000);
	CHECK(call_events(false) == 0 && view_has("heard=1") && audio_on && audio_changes == 1);
	run(HF6853_HEARD_QUIET_MS - 5000 - 100);
	CHECK(call_events(true) == 1 && call_events(false) == 0);
	CHECK(view_has("heard_gap_max_ms=1020"));
	unsigned base = n_sent;
	run(200);
	CHECK(call_events(false) == 1 && view_has("heard=0") && view_has("heard_quiet=1"));	// the fallback
	CHECK(!audio_on && audio_changes == 2);
	CHECK(count_sent(base, 0x62) == 1);	// 0x62 again, as after a 0x6F
	// 0x6F ends a heard call at once, closes the speaker and sends 0x62 again (5.4.1)
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, a, sizeof(a));
	run(20);
	CHECK(call_events(true) == 2 && view_has("heard=1") && audio_on);
	run(2500);
	CHECK(call_events(false) == 1 && audio_on);		// not ended after 2 s
	base = n_sent;
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD_END, a, sizeof(a));
	run(20);
	CHECK(call_events(false) == 2 && view_has("heard=0") && view_has("heard_ends=1") && view_has("heard_quiet=1"));
	CHECK(!audio_on && audio_changes == 4);
	run(200);
	CHECK(count_sent(base, 0x62) == 1 && last_sent(0x62)->f[8] == 1 && last_sent(0x62)->f[9] == 0);
	base = n_sent;
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD_END, NULL, 0);	// with no call heard: no event, 0x62 as stock
	run(200);
	CHECK(call_events(false) == 2 && view_has("heard_ends=1") && count_sent(base, 0x62) == 1 && !audio_on);
	// The other call events are counted and kept in the S/R ring, newest first
	chip_report(HF6853_CMD_CALL, 0x61, NULL, 0);
	chip_report(HF6853_CMD_CALL, 0x73, a, sizeof(a));
	run(20);
	CHECK(call_events(true) == 2 && view_has("heard_ignored=2") && view_has("sr06=73,61,6f,60"));
	CHECK(!view_has("last_report=06"));
	// Another call while one is heard: the first ends, the second starts
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, a, sizeof(a));
	static const uint8_t b[] = { 1, 0x00, 0x00, 0x00, 0x91, 0x00, 0x00, 0x00, 0x42 };
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, b, sizeof(b));
	run(20);
	CHECK(call_events(true) == 4 && call_events(false) == 3);
	CHECK(last_call()->u.call.call_type == DMRC_CALL_PRIVATE && last_call()->u.call.src == 42);
	CHECK(audio_on);	// one call after the other: the speaker stays open
	// While a PTT down is in flight, a 0x60 event ends its wait (the stock rule) and is still a call heard
	mute_cmd = HF6853_CMD_CALL;
	hf6853_call(DMRC_CALL_GROUP, 9, true);
	run(150);		// after the gap behind the level poll of the call heard
	CHECK(view_has("on_air=1") && !audio_on);	// our own transmission: the speaker closed
	unsigned states = count_ev(DMRC_EV_STATE), starts = call_events(true);
	static const uint8_t c3[] = { 2, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x43 };
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, c3, sizeof(c3));
	run(20);
	CHECK(count_ev(DMRC_EV_STATE) == states + 1 && last_state(DMRC_ST_TX) && call_events(true) == starts + 1);
	CHECK(view_has("last_reply=06/60"));
	mute_cmd = 0;
	hf6853_call(DMRC_CALL_GROUP, 9, false);
	run(300);
	// A short 0x60 is kept as a report
	static const uint8_t shrt[] = { 2, 0, 0 };
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, shrt, sizeof(shrt));
	run(20);
	CHECK(view_has("last_report=06/2/96/3:020000"));
	// Powered off: the call heard ends, the speaker closes
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, a, sizeof(a));
	run(20);
	CHECK(audio_on);
	unsigned ends = call_events(false);
	hf6853_power(false);
	CHECK(call_events(false) == ends + 1 && !audio_on);
}

static unsigned rssi_events(void)
{
	return count_ev(DMRC_EV_RSSI);
}

static void test_rssi(void)
{
	reset();
	bring_up();
	dmrc_channel_t ch = { .rx_hz = 438500000u, .tx_hz = 438500000u, .cc = 1, .ts = 1, .tx_id = 91 };
	hf6853_channel(&ch);
	run(1000);
	int16_t dbm = 0;
	// No signal: nothing asked, no level
	unsigned base = n_sent;
	run(2000);
	CHECK(count_sent(base, HF6853_CMD_RSSI) == 0 && !hf6853_rssi(&dbm) && rssi_events() == 0);
	// 0x59 1: the level at once, then every 500 ms; 0x05 asks with one byte, 0x02, in a checked frame
	static const uint8_t on = 1, off = 0;
	chip_report(HF6853_CMD_CARRIER, 0, &on, 1);
	run(20);
	const sent_t *q = last_sent(HF6853_CMD_RSSI);
	CHECK(q != NULL && q->f[2] == HF6853_RW_WRITE && q->f[3] == 0x01 && q->f[7] == 1 && q->f[8] == 0x02 &&
	      frame_ok(q) && (q->f[4] | q->f[5]) != 0);
	CHECK(view_has("carrier=1") && view_has("rssi_raw=40"));
	CHECK(rssi_events() == 1 && evs[n_ev - 1].u.rssi_dbm == hf6853_rssi_dbm(40));
	CHECK(hf6853_rssi(&dbm) && dbm == hf6853_rssi_dbm(40));
	CHECK(!view_has("last_reply=05"));	// the PTT replies' key stays the PTT's
	base = n_sent;
	run(2000);
	unsigned polls = count_sent(base, HF6853_CMD_RSSI);
	CHECK(polls >= 3 && polls <= 5);
	CHECK(rssi_events() == 1);		// the same level: no new event
	rssi_level = 57;			// stronger: a new event
	run(600);
	CHECK(rssi_events() == 2 && evs[n_ev - 1].u.rssi_dbm == -60 && view_has("rssi_raw=57"));
	// A missed reply is not tried again, nor counted against the link
	mute_cmd = HF6853_CMD_RSSI;
	base = n_sent;
	run(3000);
	polls = count_sent(base, HF6853_CMD_RSSI);
	CHECK(polls >= 5 && polls <= 7 && view_has("give_ups=0") && count_ev(DMRC_EV_FAULT) == 0 && hf6853_is_ready());
	mute_cmd = 0;
	run(600);
	// PTT always wins: a PTT pressed just after a poll went out follows it after the gap alone, and no poll goes out
	// while we transmit
	base = n_sent;
	for (unsigned i = 0; i < 100 && (n_sent == base || sent[n_sent - 1].f[1] != HF6853_CMD_RSSI); i++) {
		run(10);
	}
	CHECK(sent[n_sent - 1].f[1] == HF6853_CMD_RSSI);
	uint32_t poll_ms = sent[n_sent - 1].ms;
	mute_cmd = HF6853_CMD_RSSI;		// even when the poll is never answered
	CHECK(hf6853_call(DMRC_CALL_GROUP, 91, true));
	base = n_sent;
	run(300);
	mute_cmd = 0;
	CHECK(count_sent(base, HF6853_CMD_CALL) == 1 && sent[base].f[1] == HF6853_CMD_CALL);
	CHECK(sent[base].ms - poll_ms <= 110);
	CHECK(view_has("on_air=1") && !hf6853_rssi(&dbm));
	base = n_sent;
	run(2000);
	CHECK(count_sent(base, HF6853_CMD_RSSI) == 0);
	hf6853_call(DMRC_CALL_GROUP, 91, false);
	run(300);
	CHECK(view_has("on_air=0"));
	run(600);
	CHECK(hf6853_rssi(&dbm));
	// 0x59 0: no more polls, no level
	chip_report(HF6853_CMD_CARRIER, 0, &off, 1);
	run(200);
	base = n_sent;
	run(2000);
	CHECK(count_sent(base, HF6853_CMD_RSSI) == 0 && !hf6853_rssi(&dbm) && view_has("carrier=0"));
	// A call heard without a 0x59: the level is asked for all the same
	static const uint8_t a[] = { 2, 0x00, 0x00, 0x00, 0x09, 0x00, 0x23, 0x45, 0x67 };
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD, a, sizeof(a));
	run(1000);
	CHECK(count_sent(base, HF6853_CMD_RSSI) >= 1 && hf6853_rssi(&dbm));
	chip_report(HF6853_CMD_CALL, HF6853_EV_HEARD_END, a, sizeof(a));
	run(200);
	CHECK(!hf6853_rssi(&dbm));
	CHECK(in_flight_violations == 0);
}

static bool view_has(const char *needle)
{
	char line[64];
	for (uint8_t i = 0; hf6853_view_line(i, line, sizeof(line)); i++) {
		if (strstr(line, needle) != NULL) {
			return true;
		}
	}
	return false;
}

static void test_view(void)
{
	reset();
	bring_up();
	CHECK(view_has("state=ready"));
	static const uint8_t ber[] = { 0xB4, 0x01 };
	chip_report(0x16, 0, ber, sizeof(ber));
	run(20);
	CHECK(view_has("last_report=16/2/0/2:b401"));
	CHECK(view_has("seen=1a,42,3e,0c,45,48,4c,25,16"));
	CHECK(view_has("carrier=0") && view_has("rssi_raw=0") && view_has("hear_all=none"));
	// A key command from the chip: its data never shows
	static const uint8_t key[] = { 1, 4, 0x5A, 0x5A, 0x5A, 0x5A };
	chip_report(HF6853_CMD_KEY, 0, key, sizeof(key));
	run(20);
	CHECK(view_has("last_report=81/2/0/6:"));
	CHECK(!view_has("5a5a"));
	hf6853_power(false);
	CHECK(view_has("state=off"));
	CHECK(!hf6853_channel(&(dmrc_channel_t){ .ts = 1 }));
}

int main(void)
{
	test_checksum();
	test_parse();
	test_commands();
	test_power_up_retry();
	test_startup();
	test_channel_change();
	test_retries();
	test_call_data();
	test_calls();
	test_heard();
	test_rssi();
	test_view();
	if (failures != 0) {
		printf("hf6853Test: %d failures\n", failures);
		return 1;
	}
	printf("hf6853Test: all passed\n");
	return 0;
}
