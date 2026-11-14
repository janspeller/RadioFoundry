// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The HF6853 call-level DMR chip (docs/spec/hardware-rt4d.md 5, docs/spec/dmr-call-level.md 7): the engine behind a
// board's hal_dmr_call_t. The board gives the chip's enable pin and its UART; the bus hand-over and the key store are
// the board's own. Runs on the radio task from poll(); nothing waits.
#ifndef HF6853_H
#define HF6853_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hal_dmr_call.h"

typedef struct {
	void (*enable)(bool on);			// the chip's CEN
	size_t (*read)(uint8_t *buf, size_t max);	// from the UART's receive ring, never waits
	// Send len bytes from frame by interrupt, clearing each once sent (a frame may hold a key); false while the
	// previous frame is still going out. frame stays the driver's until busy() is false
	bool (*write)(uint8_t *frame, size_t len);
	bool (*busy)(void);
	// Optional: the chip's decoded audio to the speaker (on) or off (5.4.2). On from a call heard's 0x60 until it
	// ends; off between calls, while we transmit and while the chip is down. Called on changes only, from poll()
	// and power()
	void (*rx_audio)(bool on);
} hf6853_io_t;

// What this driver offers today: calls both ways (5.4.2), the keys, hear all (0x62, 5.4.1) and the signal level
// (5.4.3). SMS and the chip's other reports wait for their formats (hardware-rt4d.md 7.3). CALL_INFO: the calls heard
// (0x60) and the call start's reply as the TX state
#define HF6853_CAPS	(DMRC_CAP_CALLS | DMRC_CAP_CALL_INFO | DMRC_CAP_ALL_CALL | DMRC_CAP_ENC_ARC | \
			 DMRC_CAP_ENC_AES128 | DMRC_CAP_ENC_AES256 | DMRC_CAP_HEAR_ALL | DMRC_CAP_RSSI)

// A call heard ends at the chip's 0x6F, as on stock (5.4.2). Only should that event be lost does it end after this
// long without a 0x60 or 0x6F: longer than a call's gap between events, and the core's TX time-out bounds a call
#ifndef HF6853_HEARD_QUIET_MS
#define HF6853_HEARD_QUIET_MS	30000u
#endif

// While a signal is present (0x59) or a call is heard, the level is asked for this often (0x05, 5.4.3)
#ifndef HF6853_RSSI_POLL_MS
#define HF6853_RSSI_POLL_MS	500u
#endif

// The raw level of 0x05 (5.4.3) to dBm, a straight line through two points. UNCALIBRATED: the scale is open
// (hardware-rt4d.md 7.3). The placeholder puts the stock application's floor (raw 21) at -120 dBm and its full bar
// (raw 57) at -60 dBm; a board measures its own and overrides these
#ifndef HF6853_RSSI_RAW_LO
#define HF6853_RSSI_RAW_LO	21
#define HF6853_RSSI_DBM_LO	(-120)
#define HF6853_RSSI_RAW_HI	57
#define HF6853_RSSI_DBM_HI	(-60)
#endif

void hf6853_init(const hf6853_io_t *io);
void hf6853_power(bool on);
bool hf6853_is_ready(void);
void hf6853_poll(uint32_t now_ms);
void hf6853_listen(dmrc_event_cb_t cb, void *ctx);
bool hf6853_channel(const dmrc_channel_t *ch);
bool hf6853_groups(const uint32_t *ids, uint8_t n);
bool hf6853_call(dmrc_call_type_t type, uint32_t id, bool on);
bool hf6853_key(const dmrc_key_t *k);
// The signal level heard now in dBm (5.4.3, uncalibrated): false without a signal or a reading, or while we transmit
bool hf6853_rssi(int16_t *dbm);
bool hf6853_view_line(uint8_t i, char *buf, uint8_t len);	// CONFIG_DEBUGLINK

// Fault codes (DMRC_EV_FAULT, dmrc.fault)
#define HF6853_FAULT_NO_READY	1u	// no 0x1A report within 3 s of power-up: powered off for 3 s, then again
#define HF6853_FAULT_LOST	2u	// requests went unanswered: powered off and up again
#define HF6853_FAULT_KILLED	3u	// the chip says it is stunned (S/R 0x07)
#define HF6853_FAULT_NO_PTT_UP	4u	// a call's PTT up went unanswered: powered off, so it can't stay on the air

#endif
