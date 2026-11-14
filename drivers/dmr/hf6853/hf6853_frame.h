// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The HF6853's UART frame and command data (docs/spec/hardware-rt4d.md 5.2, 5.4, 5.4.1, 5.4.2, 5.5): head, command,
// R/W, status, checksum, length, data, tail, big-endian. Pure functions, no state of their own: the host tests build
// them.
#ifndef HF6853_FRAME_H
#define HF6853_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hal_dmr_call.h"

#define HF6853_HEAD		0x68u
#define HF6853_TAIL		0x10u
#define HF6853_HDR		8u	// head to LEN; DATA starts here
#define HF6853_OVERHEAD		9u	// a frame is LEN + 9 bytes

#define HF6853_RW_READ		0x00u
#define HF6853_RW_WRITE		0x01u
#define HF6853_RW_REPORT	0x02u	// sent by the chip on its own

// S/R in a reply
#define HF6853_SR_DONE		0x00u
#define HF6853_SR_BUSY		0x01u	// busy or failed
#define HF6853_SR_NO_CHANNEL	0x02u
#define HF6853_SR_KILLED	0x07u	// stunned
#define HF6853_SR_BAD_CHECK	0x09u
// S/R in a call request (5.4.2)
#define HF6853_SR_PTT_DOWN	0x01u
#define HF6853_SR_PTT_UP	0xFFu
// Event codes in S/R of a 0x06 frame from the chip (5.4.2). Any 0x06 frame also ends the wait for our own 0x06
#define HF6853_EV_FIRST		0x60u
#define HF6853_EV_HEARD		0x60u	// a call heard: type, called ID, caller ID; receive audio open
#define HF6853_EV_HEARD_END	0x6Fu	// the heard call's end: receive audio closed
#define HF6853_EV_LAST		0x75u

// Commands this driver sends or reads (5.4, 5.4.1, 5.4.2, 5.5)
#define HF6853_CMD_RSSI		0x05u	// the signal level: DATA 0x02 asks, the reply's DATA[0] is the raw level
#define HF6853_CMD_CALL		0x06u	// call start / stop, and the call events
#define HF6853_CMD_PSAVE	0x0Cu	// power saving, 0 off
#define HF6853_CMD_READY	0x1Au	// initialization complete (report)
#define HF6853_CMD_VERSION	0x25u
#define HF6853_CMD_OWN_ID	0x2Au	// our own radio ID
#define HF6853_CMD_CARRIER	0x59u	// report: DATA[0] 1 a signal present, 0 gone (5.4.3)
#define HF6853_CMD_PROMISC	0x62u	// promiscuous mode (hear all): DATA 1, then 1 on or 0 off (5.4.1)
#define HF6853_CMD_KEY		0x81u
#define HF6853_CMD_CHANNEL	0x82u

#define HF6853_CHANNEL_LEN	20u
#define HF6853_CALL_LEN		5u
#define HF6853_HEARD_LEN	9u	// a 0x60 event's DATA
#define HF6853_KEY_DATA_MAX	(2u + DMRC_KEY_MAX)
// The longest frame this driver sends: the AES-256 key (5.4.1). An SMS (0x07) would be longer; its limit is open
#define HF6853_TX_MAX		(HF6853_KEY_DATA_MAX + HF6853_OVERHEAD)
// The longest DATA kept from the chip; a longer frame is counted and skipped. Every report this driver reads is
// shorter; received SMS (0x11) have an open limit and aren't read yet
#ifndef HF6853_RX_DATA_MAX
#define HF6853_RX_DATA_MAX	48u
#endif

// The checksum of a whole frame of len bytes, its checksum field (bytes 4, 5) taken as zero: the 16-bit one's
// complement sum of big-endian words (an odd last byte is a word's high byte), carries folded in, inverted
uint16_t hf6853_checksum(const uint8_t *frame, size_t len);

// The frame around len DATA bytes already at f + HF6853_HDR: head, cmd, rw, sr, LEN, the tail and the checksum
// (0x0000, the chip's "no check", when check is false). Returns the frame's length
size_t hf6853_frame_build(uint8_t *f, uint8_t cmd, uint8_t rw, uint8_t sr, uint16_t len, bool check);
// The same with S/R 0x01, as every request but PTT up has it
static inline size_t hf6853_frame_finish(uint8_t *f, uint8_t cmd, uint8_t rw, uint16_t len, bool check)
{
	return hf6853_frame_build(f, cmd, rw, 0x01u, len, check);
}

// The receiver: bytes in, whole frames out
typedef struct {
	uint8_t buf[HF6853_HDR + HF6853_RX_DATA_MAX + 1u];
	uint16_t pos;
	uint16_t skip;			// bytes of a frame too long to keep, still to come
	uint32_t bad;			// a wrong tail or checksum
	uint32_t too_long;
	uint32_t unchecked;		// accepted with checksum 0x0000 (5.2)
} hf6853_rx_t;

// One byte in. True when it ends a good frame: rx->buf holds it until the next byte (hf6853_rx_* below)
bool hf6853_rx_byte(hf6853_rx_t *rx, uint8_t b);

static inline uint8_t hf6853_rx_cmd(const hf6853_rx_t *rx) { return rx->buf[1]; }
static inline uint8_t hf6853_rx_rw(const hf6853_rx_t *rx) { return rx->buf[2]; }
static inline uint8_t hf6853_rx_sr(const hf6853_rx_t *rx) { return rx->buf[3]; }
static inline uint16_t hf6853_rx_len(const hf6853_rx_t *rx) { return (uint16_t)(rx->buf[6] << 8 | rx->buf[7]); }
static inline const uint8_t *hf6853_rx_data(const hf6853_rx_t *rx) { return rx->buf + HF6853_HDR; }

// --- Command data (hf6853_cmd.c), each written to out, the length returned ---

// 0x82, 20 bytes (5.4.1). Bytes 17 and 18 are not identified yet and go as 0
size_t hf6853_cmd_channel(uint8_t *out, const dmrc_channel_t *ch);
// 0x81: key number, type, the key bytes; NULL or type off turns encryption off (2 bytes). At most HF6853_KEY_DATA_MAX
size_t hf6853_cmd_key(uint8_t *out, const dmrc_key_t *k);
// 0x2A, 4 bytes (5.4.1): our own radio ID as 8 packed BCD digits, little-endian (ID 1234567 is 67 45 23 01)
size_t hf6853_cmd_own_id(uint8_t *out, uint32_t id);
// 0x06, 5 bytes (5.4.2): the call type (1 private, 2 group, 4 all call), the target ID as 8 packed BCD digits, most
// significant first; all call sends 0xAA 0xAA 0xAA 0xAA. PTT down and up send the same bytes, S/R tells them apart
size_t hf6853_cmd_call(uint8_t *out, dmrc_call_type_t type, uint32_t id);

// A call heard (a 0x06 event 0x60, 5.4.2): the type, the called and the caller's IDs. The IDs are taken as packed BCD
// like the TX side (likely, not checked: 5.4.2); a field that isn't BCD is taken as a big-endian number and counted in
// *not_bcd, and an all call's target is DMRC_ID_ALL. False when the DATA is too short
typedef struct {
	uint8_t type;		// dmrc_call_type_t
	uint32_t dst, src;
} hf6853_heard_t;
bool hf6853_parse_heard(const uint8_t *data, uint16_t len, hf6853_heard_t *out, uint32_t *not_bcd);

// The start-up after the chip's 0x1A report (5.5): command, wait before it, length, every byte the same. 0x57 is left
// out: its two bytes are open
typedef struct {
	uint8_t cmd;
	uint16_t wait_ms;
	uint8_t len;
	uint8_t fill;
} hf6853_step_t;
extern const hf6853_step_t hf6853_startup[];
extern const uint8_t hf6853_startup_steps;

// How long the chip may take to answer cmd (5.3)
uint16_t hf6853_reply_ms(uint8_t cmd);

// 0x05's raw level (5.4.3) to dBm on the HF6853_RSSI_* line (hf6853.h), within -160 .. 0
int16_t hf6853_rssi_dbm(uint8_t raw);

#endif
