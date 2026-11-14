// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The call-level DMR contract (docs/spec/dmr-call-level.md 2): for a DMR chip that runs the air interface, the vocoder
// and call control itself. The core sees calls, IDs and settings, never bursts. Every operation queues a request and
// returns at once (true: taken); results come back as events from poll(), on the radio task. Nothing here blocks.
#ifndef HAL_DMR_CALL_H
#define HAL_DMR_CALL_H

#include <stdint.h>
#include <stdbool.h>

// What the chip offers (hal_dmr_call_t.caps)
#define DMRC_CAP_SMS		0x0001u
#define DMRC_CAP_RADIO_CHECK	0x0002u
#define DMRC_CAP_REMOTE_MON	0x0004u
#define DMRC_CAP_STUN		0x0008u
#define DMRC_CAP_CALL_ALERT	0x0010u
#define DMRC_CAP_EMERGENCY	0x0020u
#define DMRC_CAP_ALL_CALL	0x0040u
#define DMRC_CAP_RSSI		0x0080u
#define DMRC_CAP_BER		0x0100u
#define DMRC_CAP_ENC_ARC	0x0200u
#define DMRC_CAP_ENC_AES128	0x0400u
#define DMRC_CAP_ENC_AES256	0x0800u
#define DMRC_CAP_HEAR_ALL	0x1000u
#define DMRC_CAP_CALLS		0x2000u	// calls can be started (PTT); without it the chip only receives
#define DMRC_CAP_CALL_INFO	0x4000u	// the chip reports calls heard (DMRC_EV_CALL) and its state (DMRC_EV_STATE)

// One channel for the chip
typedef struct {
	uint32_t rx_hz, tx_hz;
	uint8_t cc;		// 0-15
	uint8_t ts;		// 1 or 2
	bool rmo;		// through a repeater
	uint8_t power;		// the board's power level index
	uint32_t own_id;	// ours: the channel's own ID where it has one, else the radio's
	uint32_t tx_id;		// the talk target's ID
	bool tx_private;
	uint8_t key_number;	// the key the channel uses (dmrc_key_t.number), 0 none
	bool hear_all;		// play every call on the slot and color code, not only the groups' (DMRC_CAP_HEAR_ALL)
} dmrc_channel_t;

typedef enum { DMRC_CALL_GROUP, DMRC_CALL_PRIVATE, DMRC_CALL_ALL } dmrc_call_type_t;
#define DMRC_ID_ALL		0xFFFFFFu	// the target of an all call heard, the call stack's all call ID

// Encryption keys (docs/spec/dmr-call-level.md 6). The bytes go to the chip as they are stored
typedef enum { DMRC_KEY_OFF, DMRC_KEY_ARC, DMRC_KEY_AES128, DMRC_KEY_AES256 } dmrc_key_type_t;
#define DMRC_KEY_MAX		32u
typedef struct {
	uint8_t number;		// 1..key_count, the key's ID
	uint8_t type;		// dmrc_key_type_t
	uint8_t bytes[DMRC_KEY_MAX];
} dmrc_key_t;
// Key bytes per type: ARC 5, AES-128 16, AES-256 32, off 0
static inline uint8_t dmrc_key_len(uint8_t type)
{
	return type == DMRC_KEY_ARC ? 5u : type == DMRC_KEY_AES128 ? 16u : type == DMRC_KEY_AES256 ? 32u : 0u;
}

typedef enum {
	DMRC_ST_IDLE,
	DMRC_ST_RX,		// receiving a call
	DMRC_ST_TX,		// transmitting
	DMRC_ST_TX_REFUSED,	// a call start was refused (busy, no permit, the chip said no)
} dmrc_state_t;

typedef enum {
	DMRC_EV_READY,		// the chip is up: send the channel (u.ready)
	DMRC_EV_FAULT,		// the chip didn't come up or stopped answering (u.fault)
	DMRC_EV_STATE,		// u.state
	DMRC_EV_CALL,		// a call heard starts or ends (u.call)
	DMRC_EV_RSSI,		// u.rssi_dbm
	DMRC_EV_BER,		// u.ber_pct
	DMRC_EV_KEY,		// the chip took a key or turned encryption off (u.key_ok)
} dmrc_event_type_t;

typedef struct {
	uint8_t type;		// dmrc_event_type_t
	union {
		struct { uint32_t version; } ready;
		struct { uint8_t reason; } fault;	// the driver's own code, for the debug link
		uint8_t state;				// dmrc_state_t
		struct {
			bool start;
			uint8_t call_type;		// dmrc_call_type_t
			uint32_t src, dst;
			bool encrypted, emergency;
		} call;
		int16_t rssi_dbm;
		uint8_t ber_pct;
		bool key_ok;
	} u;
} dmrc_event_t;

typedef void (*dmrc_event_cb_t)(const dmrc_event_t *ev, void *ctx);

typedef struct {
	uint32_t caps;		// DMRC_CAP_*
	void (*init)(void);
	// On: power the chip, give it the RF chip, wait for it (DMRC_EV_READY). Off: power it down
	void (*power)(bool on);
	bool (*is_ready)(void);
	void (*poll)(uint32_t now_ms);
	void (*listen)(dmrc_event_cb_t cb, void *ctx);
	bool (*channel)(const dmrc_channel_t *ch);
	bool (*groups)(const uint32_t *ids, uint8_t n);
	bool (*call)(dmrc_call_type_t type, uint32_t id, bool on);
	// NULL: encryption off. The key is copied; the caller clears its buffer
	bool (*key)(const dmrc_key_t *k);
	bool (*volume)(uint8_t level);		// optional, 0-15
	bool (*mic_gain)(uint8_t level);	// optional, 0-15
	// The key store (optional): where the radio keeps its keys, in the radio's own record format. key_get fills *out
	// (false: empty or unreadable); key_put writes one (type DMRC_KEY_OFF clears it). Never in the debug link's reads
	uint8_t key_count;
	bool (*key_get)(uint8_t number, dmrc_key_t *out);
	bool (*key_put)(const dmrc_key_t *k);
	// Optional: line i of the driver's debug link keys ("key=value", no key bytes); false past the last line
	bool (*view_line)(uint8_t i, char *buf, uint8_t len);
} hal_dmr_call_t;

#endif
