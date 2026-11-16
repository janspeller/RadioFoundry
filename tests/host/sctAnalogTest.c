// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SCT analog-call sequencing against the Packet Interface's documented commands.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modem/sct/sct_analog.h"

static uint8_t sent[32][SCT_PKT_MAX];
static uint16_t sizes[32];
static unsigned count;
static bool fail_send;
static bool send_packet(const uint8_t *pkt, uint16_t len, void *ctx)
{
	(void)ctx;
	if (fail_send) return false;
	assert(count < 32 && len <= SCT_PKT_MAX);
	memcpy(sent[count], pkt, len); sizes[count++] = len;
	return true;
}
static void ack(sct_analog_t *a, uint8_t id, uint8_t code)
{
	uint8_t p[] = { 0x84,0xA9,0x61,0,2,0,id,code };
	sct_analog_packet(a, p, sizeof(p));
}
static void expect(sct_analog_t *a, uint32_t now, uint8_t type, uint8_t id, const uint8_t *data, unsigned n)
{
	unsigned before = count;
	sct_analog_poll(a, now);
	assert(count == before + 1);
	const uint8_t *p = sent[before];
	assert(p[0] == 0x84 && p[1] == 0xA9 && p[2] == 0x61);
	assert(p[3] == 0 && p[4] == n + 1 && p[5] == type && p[6] == id);
	assert(sizes[before] == ((7 + n + 1) & ~1u));
	assert(n == 0 || memcmp(p + 7, data, n) == 0);
	ack(a, id, 0);
}
static void expect_codec(sct_analog_t *a, uint32_t now, uint8_t reg, uint8_t value, uint8_t status)
{
	unsigned before = count;
	sct_analog_poll(a, now);
	assert(count == before + 1 && sizes[before] == 12);
	const uint8_t *p = sent[before];
	assert(p[4] == 6 && p[5] == 0x40 && p[6] == 0 && p[7] == 1);
	assert(p[8] == reg && p[9] == value && p[10] == 0 && p[11] == 0);
	ack(a, 0x17, 0); // an unrelated control ACK cannot complete a codec write
	assert(a->pending && a->pending_codec);
	uint8_t answer[] = { 0x84,0xA9,0x61,0,2,0x40,0x17,status };
	sct_analog_packet(a, answer, sizeof(answer));
}
static void setup_rx(sct_analog_t *a, bool narrow)
{
	uint8_t vocoder[] = { 16 }, mode[] = { 0x80 }, cfg[] = { 0,8,0,16,0,16,0,16,0,8,0 };
	uint8_t sub[] = { 0x40,0 }, rx[] = { 1,0,0 }, carrier[] = { 1 };
	cfg[0] = narrow ? 0 : 0x0A;
	if (!a->analog_loaded) expect(a, 0, 0, 0x10, vocoder, 1);
	expect(a, 0, 0, 0x1A, mode, 1);
	for (unsigned i = 0; i < a->setup_count; i++) {
		const sct_field_t *f = &a->setup_fields[i];
		expect(a, 1, 0, f->id, f->data, f->len);
	}
	expect(a, 2, 0, 0x3C, cfg, 11);
	expect(a, 4, 0, 0x26, sub, 2);
	expect(a, 6, 0, 0x18, rx, 3);
	expect(a, 8, 0, 0x19, carrier, 1);
	sct_analog_poll(a, 10);
	assert(a->ready && !a->pending && !a->error);
}
int main(void)
{
	sct_analog_t a;
	sct_analog_init(&a, send_packet, NULL);
	sct_analog_poll(&a, 0); assert(count == 0);
	sct_analog_request(&a, true, false, true);
	setup_rx(&a, true);
	for (unsigned i = 0; i < 20; i++) sct_analog_poll(&a, 20 + i);
	assert(count == 6); // receiving cannot start a transmission or spam commands

	uint8_t tx[] = { 2,0,0 }, rx[] = { 1,0,0 }, carrier[] = { 1 };
	sct_analog_request(&a, true, true, true);
	expect(&a, 50, 0, 0x18, tx, 3);
	expect(&a, 52, 5, 0x78, NULL, 0);
	sct_analog_poll(&a, 54); assert(a.ready);
	sct_analog_request(&a, true, false, true);
	expect(&a, 56, 0, 0x21, NULL, 0);
	expect(&a, 58, 0, 0x18, rx, 3);
	expect(&a, 60, 0, 0x19, carrier, 1);
	sct_analog_poll(&a, 62); assert(a.ready);

	// A short press released while WORK_MODE is pending must not emit CALL_START afterward.
	sct_analog_request(&a, true, true, true);
	sct_analog_poll(&a, 64); assert(a.pending == 0x18);
	sct_analog_request(&a, true, false, true);
	ack(&a, 0x18, 0);
	expect(&a, 66, 0, 0x18, rx, 3);
	expect(&a, 68, 0, 0x19, carrier, 1);
	sct_analog_poll(&a, 70); assert(a.ready);

	// New bandwidth reconfigures the shared filters; disabling relinquishes ownership without writes.
	sct_analog_request(&a, true, false, false);
	setup_rx(&a, false);
	unsigned before = count;
	sct_analog_request(&a, false, false, false);
	assert(!a.ready && !a.digital_ready && sct_analog_owns_link(&a));
	uint8_t digital[] = { 4 };
	expect(&a, 80, 0, 0x10, digital, 1);
	assert(a.digital_ready && !a.analog_loaded && !sct_analog_owns_link(&a));
	sct_analog_poll(&a, 82); assert(count == before + 1);

	// Finish an outstanding analog-vocoder selection before issuing digital restore with the same field ID.
	count = 0;
	sct_analog_init(&a, send_packet, NULL);
	sct_analog_request(&a, true, false, true);
	sct_analog_poll(&a, 0);
	assert(a.pending == 0x10 && !a.digital_ready);
	sct_analog_request(&a, false, false, true);
	sct_analog_poll(&a, 2); assert(count == 1);
	ack(&a, 0x10, 0);
	assert(a.analog_loaded && !a.digital_ready);
	expect(&a, 4, 0, 0x10, digital, 1);
	assert(a.digital_ready && !sct_analog_owns_link(&a));

	count = 0;
	sct_analog_init(&a, send_packet, NULL);
	sct_analog_request(&a, true, false, true);
	sct_analog_poll(&a, UINT32_MAX - 50u);
	ack(&a, 0x19, 0); assert(a.pending == 0x10);
	uint8_t codec[] = { 0x84,0xA9,0x61,0,2,0x40,0x17,4 };
	sct_analog_packet(&a, codec, sizeof(codec)); assert(a.pending == 0x10);
	sct_analog_poll(&a, 2949); assert(a.error == 255 && a.timeouts == 1 && !a.ready);
	sct_analog_poll(&a, 2951); assert(count == 1);
	sct_analog_request(&a, true, false, true);
	sct_analog_poll(&a, 2953); assert(count == 2);
	ack(&a, 0x17, 4); assert(a.error == 4 && !a.pending);
	sct_analog_poll(&a, 2955); assert(count == 2);
	sct_analog_request(&a, true, false, true);
	fail_send = true;
	sct_analog_poll(&a, 2957); assert(a.error == 254 && !a.ready);
	count = 0; fail_send = false;
	sct_analog_init(&a, send_packet, NULL);
	static const sct_codec_write_t mic[] = { { 1,52,0x80 }, { 1,59,0x14 } };
	a.tx_codec = mic; a.tx_codec_count = 2;
	sct_analog_request(&a, true, false, true);
	setup_rx(&a, true);
	sct_analog_request(&a, true, true, true);
	expect_codec(&a, 20, 52, 0x80, 0);
	assert(a.tx_codec_index == 1 && !a.pending);
	expect_codec(&a, 22, 59, 0x14, 0);
	expect(&a, 24, 0, 0x18, tx, 3);
	expect(&a, 26, 5, 0x78, NULL, 0);
	sct_analog_poll(&a, 28); assert(a.ready);
	sct_analog_request(&a, true, false, true);
	expect(&a, 30, 0, 0x21, NULL, 0);
	expect(&a, 32, 0, 0x18, rx, 3);
	expect(&a, 34, 0, 0x19, carrier, 1);
	sct_analog_poll(&a, 36);
	// Routing must be reapplied every press; a refusal must prevent WORK_MODE TX and call start.
	sct_analog_request(&a, true, true, true);
	expect_codec(&a, 38, 52, 0x80, 7);
	before = count;
	sct_analog_poll(&a, 40);
	assert(a.error == 7 && !a.ready && count == before);
	// Releasing during microphone preparation must not key the modem after the codec ACK.
	count = 0;
	sct_analog_init(&a, send_packet, NULL);
	a.tx_codec = mic; a.tx_codec_count = 2;
	sct_analog_request(&a, true, false, true);
	setup_rx(&a, true);
	sct_analog_request(&a, true, true, true);
	sct_analog_poll(&a, 20);
	sct_analog_request(&a, true, false, true);
	uint8_t mic_ack[] = { 0x84,0xA9,0x61,0,2,0x40,0x17,0 };
	sct_analog_packet(&a, mic_ack, sizeof(mic_ack));
	before = count;
	sct_analog_poll(&a, 22);
	assert(a.ready && count == before);

#if CONFIG_DIAG
	count = 0; fail_send = false;
	sct_analog_init(&a, send_packet, NULL);
	assert(!sct_analog_loopback(&a, true));
	sct_analog_request(&a, true, false, true);
	setup_rx(&a, true);
	assert(sct_analog_loopback(&a, true));
	assert(!sct_analog_loopback(&a, false)); // do not overlap outstanding setup
	uint8_t loop[] = { 3,0x11,0 };
	expect(&a, 20, 0, 0x18, loop, 3);
	expect(&a, 22, 0, 0x19, carrier, 1);
	sct_analog_poll(&a, 24);
	assert(a.ready && a.loopback);
	before = count;
	for (unsigned i = 0; i < 20; i++) sct_analog_poll(&a, 26 + i);
	assert(count == before); // loopback never sends TX work or CALL_START
	assert(sct_analog_loopback(&a, false));
	expect(&a, 50, 0, 0x18, rx, 3);
	expect(&a, 52, 0, 0x19, carrier, 1);
	sct_analog_poll(&a, 54);
	assert(a.ready && !a.loopback);
	assert(sct_analog_loopback(&a, true));
	expect(&a, 56, 0, 0x18, loop, 3);
	expect(&a, 58, 0, 0x19, carrier, 1);
	sct_analog_poll(&a, 60);
	sct_analog_request(&a, true, false, true);
	assert(!a.loopback);
	setup_rx(&a, true); // a retune cancels the diagnostic
#endif
	count = 0;
	sct_analog_init(&a, send_packet, NULL);
	static const uint8_t thresholds[12] = {0,0,0,60,15,60,10,140,255,255,255,255};
	static const sct_field_t fields[] = {{SCT_F_SQ_LEVEL,12,thresholds}};
	a.setup_fields = fields; a.setup_count = 1;
	sct_analog_request(&a,true,false,true);
	setup_rx(&a,true);
	before = count;
	sct_analog_poll(&a,100); assert(count==before);
	sct_analog_request(&a,true,true,true);
	expect(&a,102,0,0x18,tx,3); // setup is not repeated at PTT
	count = 0;
	sct_analog_init(&a, send_packet, NULL);
	a.setup_fields = fields; a.setup_count = 1;
	sct_analog_request(&a,true,false,true);
	uint8_t av[] = {16}, am[] = {128};
	expect(&a,0,0,0x10,av,1);
	expect(&a,2,0,0x1A,am,1);
	sct_analog_poll(&a,4); ack(&a,0x6C,6);
	before = count;
	sct_analog_poll(&a,6);
	assert(a.error==6 && !a.ready && count==before);
	puts("sctAnalogTest PASSED");
	return 0;
}
