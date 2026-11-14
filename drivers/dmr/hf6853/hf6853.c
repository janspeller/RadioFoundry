// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The HF6853 engine (docs/spec/hardware-rt4d.md 5.3, 5.4.1, 5.4.2, 5.4.3, 5.5): the power-up wait, the stock start-up
// commands, one request in flight with the stock pacing and retries, the channel change in the stock order, calls
// both ways, the signal level. The chip's reports whose formats are open are counted and kept for the debug link,
// never guessed at.
//
// Channel change, as the stock application sends it: 0x2A (our own ID), 0x82 (channel), the receive group, 0x81
// (key), 0x62 (promiscuous: the core's hear all), 0x0C (power saving off). The receive group (0x17 / 0x84) is left
// out until its bytes are known (7.3): the chip keeps its own default. A channel the chip already has isn't sent
// again; a change of hear all alone sends 0x62 alone.
//
// Calls (5.4.2): PTT down and up are one 0x06 frame each, S/R 0x01 and 0xFF. PTT up goes ahead of anything else
// queued; PTT down after the channel's set-up. Any 0x06 frame from the chip answers it, as on stock (5.4.2): only
// "killed" and "no channel" refuse a PTT down. Once a PTT down went out the call counts as possibly on the air: PTT up
// follows it in every case, and a PTT up the chip never answers powers the chip off. A call heard starts with the
// event 0x60 and ends with 0x6F, as on stock, which then sends 0x62 again; HF6853_HEARD_QUIET_MS without either is
// only a fallback for a lost 0x6F. The board's speaker follows the call heard (io->rx_audio).
//
// Signal level (5.4.3): while the chip reports a signal (0x59) or a call is heard, 0x05 asks for the level every
// HF6853_RSSI_POLL_MS, last in the queue and tried once: a PTT always goes first, and a missed reply waits for the
// next turn.
#include <string.h>
#include "config.h"
#include "hf6853.h"
#include "hf6853_frame.h"
#include "debuglink/dl_fmt.h"

#define GAP_MS		100u	// after a request, before the next (5.3)
#define TRIES		6u
#define READY_WAIT_MS	3000u	// for the 0x1A report after power-up (5.5)
#define COOL_MS		3000u	// powered off after a missed 0x1A, before trying again
#define LOST_AFTER	3u	// requests given up in a row before the chip counts as lost
#define SEEN_MAX	16u
#define SR_REQUEST	0x01u	// S/R in a request (5.2)

typedef enum { E_OFF, E_WAIT, E_COOL, E_STARTUP, E_READY } eng_t;

// What is sent in ready state; PTT up first, then the stock channel change, then PTT down
#define B_OWN_ID	0x01u
#define B_CHANNEL	0x02u
#define B_KEY		0x04u
#define B_PSAVE		0x08u
#define B_PTT_DOWN	0x10u
#define B_PTT_UP	0x20u
#define B_PROMISC	0x40u
#define B_RSSI		0x80u

#define PROMISC_NONE	0xFFu	// d.promisc: no 0x62 taken since the chip came up

typedef enum {
	R_NONE, R_STEP, R_VERSION, R_OWN_ID, R_CHANNEL, R_KEY, R_PSAVE, R_PTT_DOWN, R_PTT_UP, R_PROMISC, R_RSSI
} req_t;

static struct {
	const hf6853_io_t *io;
	dmrc_event_cb_t cb;
	void *ctx;
	uint8_t st;		// eng_t
	uint32_t now;
	uint32_t t;		// when the state or the start-up step began
	bool stamp;		// t is set at the next poll: power() runs between polls, whose clock it doesn't have
	uint8_t step;		// start-up step
	uint8_t pending;	// B_*
	uint8_t req;		// req_t in flight, R_NONE none
	uint8_t req_cmd;
	uint8_t last_req;	// the request the tries count for
	uint8_t tries;
	bool sent;		// a request was ever sent since power-up (the gap counts from last_tx)
	uint32_t last_tx;
	uint8_t lost;		// give-ups in a row
	uint32_t version;
	uint8_t own_id[4];	// our own ID (0x2A)
	uint8_t channel[HF6853_CHANNEL_LEN];
	bool chan_sent;		// the chip took own_id and channel as they are
	bool hear_all;		// what 0x62 should say
	uint8_t promisc;	// what the last 0x62 sent said, PROMISC_NONE before one was taken
	uint8_t key[HF6853_KEY_DATA_MAX];	// held until the chip took it or it was given up, then cleared
	uint8_t key_len;
	uint8_t groups;		// the receive group list's length (not sent yet)
	uint8_t call[HF6853_CALL_LEN];	// the call's 0x06 DATA, for PTT down and up alike
	bool on_air;		// a PTT down went out and no PTT up was answered since
	bool heard_on;		// a call heard, not ended yet
	hf6853_heard_t heard;
	uint32_t heard_last;	// the last 0x60 event for it
	bool audio;		// io->rx_audio's last level
	bool carrier;		// the chip's 0x59 report: a signal present
	bool level;		// rssi_raw is a reading of the signal heard now
	uint8_t rssi_raw;	// the last 0x05 reply's level
	uint32_t rssi_t;	// when 0x05 was last asked for
	uint8_t tx[HF6853_TX_MAX];
	hf6853_rx_t rx;
	// For the debug link
	uint32_t power_ups, frames_tx, frames_rx, timeouts, give_ups, nacks, reports;
	uint32_t heard_events, heard_not_bcd, heard_ignored, heard_gap_max, heard_ends, heard_quiet;
	uint8_t last_reply[2];	// the last reply to one of our requests: cmd, S/R
	uint8_t sr06[4];	// the last distinct S/R values on 0x06 from the chip, newest first
	uint8_t sr06_n;
	uint8_t heard_raw[HF6853_HEARD_LEN];	// the last 0x60 event's DATA, as received
	uint8_t last[4];	// the last report: cmd, R/W, S/R, LEN (low byte)
	uint8_t last_data[8];
	uint8_t seen[SEEN_MAX];	// the commands the chip sent, in the order first seen
	uint8_t seen_n;
} d;

static void wipe(void *p, size_t n)
{
	volatile uint8_t *b = p;
	while (n-- != 0) {
		*b++ = 0;
	}
}

static void emit(const dmrc_event_t *ev)
{
	if (d.cb != NULL) {
		d.cb(ev, d.ctx);
	}
}

static void fault(uint8_t reason)
{
	dmrc_event_t ev = { .type = DMRC_EV_FAULT, .u.fault.reason = reason };
	emit(&ev);
}

static void tx_state(uint8_t state)
{
	dmrc_event_t ev = { .type = DMRC_EV_STATE, .u.state = state };
	emit(&ev);
}

static void heard_event(bool start)
{
	dmrc_event_t ev = { .type = DMRC_EV_CALL };
	ev.u.call.start = start;
	ev.u.call.call_type = d.heard.type;
	ev.u.call.src = d.heard.src;
	ev.u.call.dst = d.heard.dst;
	emit(&ev);
}

static void heard_end(void)
{
	if (d.heard_on) {
		d.heard_on = false;
		heard_event(false);
	}
}

static void drop_key(void)
{
	wipe(d.key, sizeof(d.key));
	d.key_len = 0;
}

static void enter(uint8_t st)
{
	d.st = st;
	d.t = d.now;
	d.req = R_NONE;
	d.tries = 0;
	d.lost = 0;
	if (st != E_READY) {
		// Off, restarting or set up again: no call is on the air or heard any more
		d.on_air = false;
		d.pending &= (uint8_t)~(B_PTT_DOWN | B_PTT_UP | B_RSSI);
		heard_end();
		d.chan_sent = false;		// a chip set up again has neither
		d.promisc = PROMISC_NONE;
		d.carrier = d.level = false;
	}
	if (st == E_WAIT) {
		d.power_ups++;
		d.io->enable(true);
	} else if (st == E_COOL || st == E_OFF) {
		d.io->enable(false);
	}
}

void hf6853_init(const hf6853_io_t *io)
{
	memset(&d, 0, sizeof(d));
	d.io = io;
	d.promisc = PROMISC_NONE;
	io->enable(false);
}

void hf6853_listen(dmrc_event_cb_t cb, void *ctx)
{
	d.cb = cb;
	d.ctx = ctx;
}

// The speaker plays the chip's audio while a call is heard, never while we transmit (5.4.2)
static void audio_sync(void)
{
	bool on = d.st == E_READY && d.heard_on && !d.on_air;
	if (on != d.audio) {
		d.audio = on;
		if (d.io->rx_audio != NULL) {
			d.io->rx_audio(on);
		}
	}
}

void hf6853_power(bool on)
{
	if (on) {
		if (d.st == E_OFF) {
			d.version = 0;
			enter(E_WAIT);
			d.stamp = true;
		}
		return;
	}
	enter(E_OFF);
	d.pending = 0;
	drop_key();
	audio_sync();
}

bool hf6853_is_ready(void)
{
	return d.st == E_READY;
}

static void send(uint8_t req, uint8_t cmd, uint8_t rw, uint8_t sr, const uint8_t *data, uint8_t len, bool check)
{
	if (data != NULL) {
		memcpy(d.tx + HF6853_HDR, data, len);
	}
	size_t n = hf6853_frame_build(d.tx, cmd, rw, sr, len, check);
	if (!d.io->write(d.tx, n)) {
		wipe(d.tx, sizeof(d.tx));
		return;		// the UART is still busy: sent at the next poll
	}
	if (req != d.last_req) {
		d.tries = 0;	// another request took the place of one being tried again
		d.last_req = req;
	}
	d.req = req;
	d.req_cmd = cmd;
	d.sent = true;
	d.last_tx = d.now;
	d.frames_tx++;
	if (req == R_PTT_DOWN) {
		d.on_air = true;
	}
}

// The request in flight is over: taken (ok) or not
static void done(bool ok)
{
	uint8_t req = d.req;
	d.req = R_NONE;
	d.tries = 0;
	d.last_req = R_NONE;
	switch (req) {
	case R_STEP:
		d.step++;
		d.t = d.now;
		break;
	case R_VERSION: {
		d.st = E_READY;
		d.t = d.now;
		dmrc_event_t ev = { .type = DMRC_EV_READY, .u.ready.version = d.version };
		emit(&ev);
		break;
	}
	case R_OWN_ID:
		d.pending &= (uint8_t)~B_OWN_ID;
		d.chan_sent = ok;	// the channel follows: both taken, or both go again at the next change
		break;
	case R_CHANNEL:
		d.pending &= (uint8_t)~B_CHANNEL;
		d.chan_sent = d.chan_sent && ok;
		break;
	case R_PROMISC:
		// hear all may have changed while this one was out: then it goes again
		d.pending &= (uint8_t)~B_PROMISC;
		if (!ok) {
			d.promisc = PROMISC_NONE;
		} else if (d.promisc != (uint8_t)d.hear_all) {
			d.pending |= B_PROMISC;
		}
		break;
	case R_RSSI:
		d.pending &= (uint8_t)~B_RSSI;
		break;
	case R_KEY: {
		d.pending &= (uint8_t)~B_KEY;
		drop_key();
		dmrc_event_t ev = { .type = DMRC_EV_KEY, .u.key_ok = ok };
		emit(&ev);
		break;
	}
	case R_PSAVE:
		d.pending &= (uint8_t)~B_PSAVE;
		break;
	case R_PTT_DOWN:
		d.pending &= (uint8_t)~B_PTT_DOWN;
		if (!ok) {
			d.pending |= B_PTT_UP;	// refused or unanswered: a PTT up all the same, in case it went out
		}
		tx_state(ok ? DMRC_ST_TX : DMRC_ST_TX_REFUSED);
		break;
	case R_PTT_UP:
		d.pending &= (uint8_t)~B_PTT_UP;
		d.on_air = false;
		tx_state(DMRC_ST_IDLE);
		if (!ok) {
			// The chip may still be transmitting: power it off, then up again
			fault(HF6853_FAULT_NO_PTT_UP);
			enter(E_COOL);
		}
		break;
	default:
		break;
	}
}

// The next request, when the gap since the last one has passed
static void next(void)
{
	if (d.req != R_NONE || (d.sent && d.now - d.last_tx < GAP_MS) || d.io->busy()) {
		return;
	}
	if (d.st == E_STARTUP) {
		if (d.step < hf6853_startup_steps) {
			const hf6853_step_t *s = &hf6853_startup[d.step];
			if (d.now - d.t >= s->wait_ms) {
				memset(d.tx + HF6853_HDR, s->fill, s->len);
				send(R_STEP, s->cmd, HF6853_RW_WRITE, SR_REQUEST, NULL, s->len, false);
			}
		} else {
			send(R_VERSION, HF6853_CMD_VERSION, HF6853_RW_READ, SR_REQUEST, NULL, 0, false);
		}
		return;
	}
	if (d.st != E_READY) {
		return;
	}
	static const uint8_t zero = 0, rssi_ask = 0x02u;
	if (d.pending & B_PTT_UP) {
		send(R_PTT_UP, HF6853_CMD_CALL, HF6853_RW_WRITE, HF6853_SR_PTT_UP, d.call, HF6853_CALL_LEN, true);
	} else if (d.pending & B_OWN_ID) {
		send(R_OWN_ID, HF6853_CMD_OWN_ID, HF6853_RW_WRITE, SR_REQUEST, d.own_id, sizeof(d.own_id), true);
	} else if (d.pending & B_CHANNEL) {
		send(R_CHANNEL, HF6853_CMD_CHANNEL, HF6853_RW_WRITE, SR_REQUEST, d.channel, HF6853_CHANNEL_LEN, true);
	} else if (d.pending & B_KEY) {
		send(R_KEY, HF6853_CMD_KEY, HF6853_RW_WRITE, SR_REQUEST, d.key, d.key_len, true);
	} else if (d.pending & B_PROMISC) {
		uint8_t p[2] = { 1u, d.hear_all ? 1u : 0u };
		d.promisc = p[1];
		send(R_PROMISC, HF6853_CMD_PROMISC, HF6853_RW_WRITE, SR_REQUEST, p, sizeof(p), true);
	} else if (d.pending & B_PSAVE) {
		send(R_PSAVE, HF6853_CMD_PSAVE, HF6853_RW_WRITE, SR_REQUEST, &zero, 1, true);
	} else if (d.pending & B_PTT_DOWN) {
		send(R_PTT_DOWN, HF6853_CMD_CALL, HF6853_RW_WRITE, HF6853_SR_PTT_DOWN, d.call, HF6853_CALL_LEN, true);
	} else if (d.pending & B_RSSI) {
		send(R_RSSI, HF6853_CMD_RSSI, HF6853_RW_WRITE, SR_REQUEST, &rssi_ask, 1, true);
	}
}

static void seen(uint8_t cmd)
{
	for (uint8_t i = 0; i < d.seen_n; i++) {
		if (d.seen[i] == cmd) {
			return;
		}
	}
	if (d.seen_n < SEEN_MAX) {
		d.seen[d.seen_n++] = cmd;
	}
}

// An S/R seen on 0x06, into the ring of the last distinct ones (the codes are partly open: 5.4.2)
static void call_sr(uint8_t sr)
{
	uint8_t i = 0;
	while (i < d.sr06_n && d.sr06[i] != sr) {
		i++;
	}
	if (i == d.sr06_n && d.sr06_n < sizeof(d.sr06)) {
		d.sr06_n++;
	}
	for (i = (uint8_t)(i < sizeof(d.sr06) ? i : sizeof(d.sr06) - 1u); i > 0; i--) {
		d.sr06[i] = d.sr06[i - 1u];
	}
	d.sr06[0] = sr;
}

// A 0x60 event: a new call heard starts (the one before it ends), the same call's again keeps it going
static bool heard_in(const uint8_t *data, uint16_t len)
{
	hf6853_heard_t h;
	if (!hf6853_parse_heard(data, len, &h, &d.heard_not_bcd)) {
		return false;
	}
	d.heard_events++;
	memcpy(d.heard_raw, data, sizeof(d.heard_raw));
	if (d.heard_on && h.type == d.heard.type && h.src == d.heard.src && h.dst == d.heard.dst) {
		uint32_t gap = d.now - d.heard_last;
		if (gap > d.heard_gap_max) {
			d.heard_gap_max = gap;
		}
		d.heard_last = d.now;
		return true;
	}
	heard_end();
	d.heard = h;
	d.heard_on = true;
	d.heard_last = d.now;
	heard_event(true);
	return true;
}

// The level from a 0x05 reply (5.4.3); the core hears of it when its dBm changes
static void rssi_in(uint8_t raw)
{
	int16_t dbm = hf6853_rssi_dbm(raw);
	bool news = !d.level || dbm != hf6853_rssi_dbm(d.rssi_raw);
	d.rssi_raw = raw;
	d.level = true;
	if (news) {
		dmrc_event_t ev = { .type = DMRC_EV_RSSI, .u.rssi_dbm = dbm };
		emit(&ev);
	}
}

// The end of a call heard by the chip's word or the fallback: 0x62 again, as stock sends it after 0x6F (5.4.1)
static void heard_over(void)
{
	heard_end();
	if (d.st == E_READY) {
		d.pending |= B_PROMISC;
	}
}

static void frame_in(void)
{
	const hf6853_rx_t *rx = &d.rx;
	uint8_t cmd = hf6853_rx_cmd(rx), rw = hf6853_rx_rw(rx), sr = hf6853_rx_sr(rx);
	uint16_t len = hf6853_rx_len(rx);
	const uint8_t *data = hf6853_rx_data(rx);
	d.frames_rx++;
	d.lost = 0;
	seen(cmd);
	if (cmd == HF6853_CMD_READY) {
		// Initialization complete with status 0. Which byte holds the status is open: S/R or the first DATA byte
		if (d.st != E_OFF && (sr == 0 || (len != 0 && data[0] == 0))) {
			enter(E_STARTUP);	// also a chip that restarted on its own: its set-up goes again
			d.step = 0;
		}
		return;
	}
	if (cmd == HF6853_CMD_CARRIER && len != 0) {
		bool on = data[0] != 0;
		if (on && !d.carrier) {
			d.rssi_t = d.now - HF6853_RSSI_POLL_MS;	// the first level at once
		}
		d.carrier = on;
		return;
	}
	if (cmd == HF6853_CMD_RSSI && (sr == HF6853_SR_DONE || d.req == R_RSSI)) {
		// Our poll's reply (S/R 0x00: the level; anything else: none this time), never tried again. Kept out of
		// last_reply, which is for the owner's PTT replies
		if (sr == HF6853_SR_DONE && len != 0) {
			rssi_in(data[0]);
		}
		if (d.req == R_RSSI) {
			done(sr == HF6853_SR_DONE);
		}
		return;
	}
	bool event = cmd == HF6853_CMD_CALL && sr >= HF6853_EV_FIRST && sr <= HF6853_EV_LAST;
	if (cmd == HF6853_CMD_CALL) {
		call_sr(sr);
	}
	// The reply to our request: any frame of its command. For 0x06 a call event too: the stock application takes any
	// 0x06 frame as the end of its wait, whatever its S/R (5.4.2)
	bool replied = false;
	if (d.req != R_NONE && cmd == d.req_cmd) {
		d.last_reply[0] = cmd;
		d.last_reply[1] = sr;
		if (sr == HF6853_SR_BUSY || sr == HF6853_SR_BAD_CHECK) {
			if (++d.tries < TRIES) {
				d.req = R_NONE;		// again after the gap
			} else {
				d.give_ups++;
				done(false);
			}
			return;
		}
		if (d.req == R_VERSION) {
			uint32_t v = 0;
			for (uint16_t i = 0; i < len && i < 4u; i++) {
				v = v << 8 | data[i];	// raw: the reply's format is open
			}
			d.version = v;
		}
		if (sr == HF6853_SR_KILLED) {
			fault(HF6853_FAULT_KILLED);
		}
		if (sr != HF6853_SR_DONE) {
			d.nacks++;
		}
		// A call's PTT down is refused only for "killed" and "no channel" (5.2); a PTT up is always taken
		bool ok = sr == HF6853_SR_DONE || d.req == R_PTT_UP ||
			  (d.req == R_PTT_DOWN && sr != HF6853_SR_KILLED && sr != HF6853_SR_NO_CHANNEL);
		done(ok);
		replied = true;
	}
	if (event) {
		if (sr == HF6853_EV_HEARD) {
			if (d.st == E_READY && heard_in(data, len)) {
				return;
			}
			// Too short to read, or before ready: kept as a report below
		} else if (sr == HF6853_EV_HEARD_END) {
			if (d.heard_on) {
				d.heard_ends++;
			}
			if (d.st == E_READY) {
				heard_over();	// 0x62 after every 0x6F, as stock sends it
			}
			return;
		} else {
			d.heard_ignored++;	// the other call events: in the S/R ring for the owner, not read yet
			return;
		}
	} else if (replied && rw != HF6853_RW_REPORT) {
		return;
	}
	d.reports++;
	d.last[0] = cmd;
	d.last[1] = rw;
	d.last[2] = sr;
	d.last[3] = (uint8_t)len;
	memset(d.last_data, 0, sizeof(d.last_data));
	if (cmd != HF6853_CMD_KEY) {
		memcpy(d.last_data, data, len < sizeof(d.last_data) ? len : sizeof(d.last_data));
	}
}

// The level is asked for while a signal or a call is heard, never while a PTT waits or we transmit
static void rssi_poll(uint32_t now_ms)
{
	if (!d.carrier && !d.heard_on) {
		d.level = false;
	}
	if ((!d.carrier && !d.heard_on) || d.on_air || (d.pending & (B_PTT_DOWN | B_PTT_UP)) != 0) {
		d.pending &= (uint8_t)~B_RSSI;
		return;
	}
	if (now_ms - d.rssi_t >= HF6853_RSSI_POLL_MS && (d.pending & B_RSSI) == 0 && d.req != R_RSSI) {
		d.rssi_t = now_ms;
		d.pending |= B_RSSI;
	}
}

static void run(uint32_t now_ms)
{
	if (d.stamp) {
		d.stamp = false;
		d.t = now_ms;
	}
	uint8_t buf[16];
	size_t n;
	while ((n = d.io->read(buf, sizeof(buf))) != 0) {
		for (size_t i = 0; i < n; i++) {
			if (hf6853_rx_byte(&d.rx, buf[i])) {
				frame_in();
			}
		}
	}
	switch (d.st) {
	case E_WAIT:
		if (now_ms - d.t >= READY_WAIT_MS) {
			fault(HF6853_FAULT_NO_READY);
			enter(E_COOL);
		}
		return;
	case E_COOL:
		if (now_ms - d.t >= COOL_MS) {
			enter(E_WAIT);
		}
		return;
	case E_STARTUP:
	case E_READY:
		break;
	default:
		return;
	}
	if (d.heard_on && now_ms - d.heard_last >= HF6853_HEARD_QUIET_MS) {
		d.heard_quiet++;
		heard_over();
	}
	if (d.st == E_READY) {
		rssi_poll(now_ms);
	}
	if (d.req != R_NONE && now_ms - d.last_tx >= hf6853_reply_ms(d.req_cmd)) {
		d.timeouts++;
		if (d.req == R_RSSI) {
			done(false);		// not tried again, nor counted against the link: the next turn asks anew
		} else if (++d.tries < TRIES) {
			d.req = R_NONE;		// sent again below: the gap has passed
		} else {
			d.give_ups++;
			done(false);
			if (d.st == E_COOL) {
				return;		// a PTT up given up powered the chip off
			}
			if (++d.lost >= LOST_AFTER && d.st == E_READY) {
				fault(HF6853_FAULT_LOST);
				enter(E_COOL);
				return;
			}
		}
	}
	next();
}

void hf6853_poll(uint32_t now_ms)
{
	d.now = now_ms;
	if (d.io == NULL) {
		return;
	}
	run(now_ms);
	audio_sync();
}

bool hf6853_channel(const dmrc_channel_t *ch)
{
	if (d.st == E_OFF) {
		return false;
	}
	uint8_t id[sizeof(d.own_id)], chan[HF6853_CHANNEL_LEN];
	hf6853_cmd_own_id(id, ch->own_id);	// the call target goes in 0x06 alone
	hf6853_cmd_channel(chan, ch);
	d.hear_all = ch->hear_all;
	// The core sends the channel on every change of its configuration (the reply window, the target): what the chip
	// already has isn't sent again, so a call heard isn't disturbed. hear all alone is 0x62 alone
	bool same = d.chan_sent && (d.pending & (B_OWN_ID | B_CHANNEL)) == 0 && memcmp(id, d.own_id, sizeof(id)) == 0 &&
		    memcmp(chan, d.channel, sizeof(chan)) == 0;
	if (!same) {
		memcpy(d.own_id, id, sizeof(id));
		memcpy(d.channel, chan, sizeof(chan));
		d.chan_sent = false;
		d.pending |= B_OWN_ID | B_CHANNEL | B_PROMISC | B_PSAVE;
	} else if (d.promisc != (uint8_t)d.hear_all && d.req != R_PROMISC) {
		d.pending |= B_PROMISC;
	}
	return true;
}

bool hf6853_groups(const uint32_t *ids, uint8_t n)
{
	(void)ids;
	d.groups = n;		// kept for the view only: the chip's group commands are open (5.4.1)
	return d.st != E_OFF;
}

bool hf6853_call(dmrc_call_type_t type, uint32_t id, bool on)
{
	if (on) {
		if (d.st != E_READY || d.on_air || (d.pending & (B_PTT_DOWN | B_PTT_UP)) != 0) {
			return false;	// not up, or the last call isn't over yet
		}
		hf6853_cmd_call(d.call, type, id);
		d.pending |= B_PTT_DOWN;
		d.pending &= (uint8_t)~B_RSSI;
		if (d.req == R_RSSI) {
			d.req = R_NONE;		// a level poll in flight doesn't hold the PTT: its late reply is still read
		}
		return true;
	}
	if (d.on_air) {
		d.pending &= (uint8_t)~B_PTT_DOWN;	// no more tries of the PTT down: the PTT up follows it
		d.pending |= B_PTT_UP;
	} else {
		d.pending &= (uint8_t)~B_PTT_DOWN;	// never went out: nothing to end
	}
	return true;
}

bool hf6853_rssi(int16_t *dbm)
{
	if (!d.level || d.on_air) {
		return false;
	}
	*dbm = hf6853_rssi_dbm(d.rssi_raw);
	return true;
}

bool hf6853_key(const dmrc_key_t *k)
{
	if (d.st == E_OFF) {
		return false;
	}
	if (d.req == R_KEY) {
		d.req = R_NONE;		// a newer key replaces the one in flight: its reply no longer counts
		d.tries = 0;
	}
	drop_key();
	d.key_len = (uint8_t)hf6853_cmd_key(d.key, k);
	d.pending |= B_KEY;
	return true;
}

#if CONFIG_DEBUGLINK
static const char *const state_name[] = { "off", "wait", "cool", "startup", "ready" };

bool hf6853_view_line(uint8_t i, char *buf, uint8_t len)
{
	// What the owner reads first (the call codes, the last reports) leads: a reader that keeps a few lines gets them
	const uint32_t *count[] = { &d.power_ups, &d.frames_tx, &d.frames_rx, &d.timeouts, &d.give_ups, &d.nacks,
				    &d.rx.bad, &d.rx.too_long, &d.rx.unchecked, &d.reports, &d.heard_events,
				    &d.heard_ends, &d.heard_quiet, &d.heard_not_bcd, &d.heard_ignored, &d.heard_gap_max };
	static const char *const count_name[] = { "power_ups", "frames_tx", "frames_rx", "timeouts", "give_ups",
						  "nacks", "rx_bad", "rx_long", "rx_unchecked", "reports",
						  "heard_events", "heard_ends", "heard_quiet", "heard_not_bcd",
						  "heard_ignored", "heard_gap_max_ms" };
	const uint8_t counts = sizeof(count_name) / sizeof(count_name[0]);
	const uint8_t lines = 12;
	switch (i) {
	case 0:
		dl_fmt(buf, len, "state=%s", state_name[d.st]);
		break;
	case 1:
		dl_fmt(buf, len, "last_reply=%02x/%02x", d.last_reply[0], d.last_reply[1]);
		break;
	case 2: {
		size_t n = dl_fmt(buf, len, "sr06=");
		for (uint8_t b = 0; b < d.sr06_n; b++) {
			n += dl_fmt(buf + n, len - n, b ? ",%02x" : "%02x", d.sr06[b]);
		}
		break;
	}
	case 3:
		dl_fmt(buf, len, "on_air=%u", d.on_air ? 1u : 0u);
		break;
	case 4:
		dl_fmt(buf, len, "heard=%u", d.heard_on ? 1u : 0u);
		break;
	case 5:
		dl_fmt(buf, len, "carrier=%u", d.carrier ? 1u : 0u);
		break;
	case 6:
		dl_fmt(buf, len, "rssi_raw=%u", d.rssi_raw);
		break;
	case 7:
		if (d.promisc == PROMISC_NONE) {
			dl_fmt(buf, len, "hear_all=none");
		} else {
			dl_fmt(buf, len, "hear_all=%u", d.promisc);
		}
		break;
	case 8: {
		// The last 0x60 event as received: type, called ID, caller ID (whether the IDs are BCD is open, 5.4.2)
		const uint8_t *r = d.heard_raw;
		dl_fmt(buf, len, "heard_raw=%02x:%02x%02x%02x%02x:%02x%02x%02x%02x", r[0], r[1], r[2], r[3], r[4], r[5],
		       r[6], r[7], r[8]);
		break;
	}
	case 9: {
		size_t n = dl_fmt(buf, len, "last_report=%02x/%u/%u/%u:", d.last[0], d.last[1], d.last[2], d.last[3]);
		for (uint8_t b = 0; b < sizeof(d.last_data) && b < d.last[3]; b++) {
			n += dl_fmt(buf + n, len - n, "%02x", d.last_data[b]);
		}
		break;
	}
	case 10: {
		size_t n = dl_fmt(buf, len, "seen=");
		for (uint8_t b = 0; b < d.seen_n; b++) {
			n += dl_fmt(buf + n, len - n, b ? ",%02x" : "%02x", d.seen[b]);
		}
		break;
	}
	case 11:
		dl_fmt(buf, len, "groups=%u", d.groups);
		break;
	default:
		if (i - lines >= counts) {
			return false;
		}
		dl_fmt(buf, len, "%s=%u", count_name[i - lines], (unsigned)*count[i - lines]);
		break;
	}
	return true;
}
#endif
