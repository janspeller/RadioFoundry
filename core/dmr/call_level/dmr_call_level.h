// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The call-level DMR backend (docs/spec/dmr-call-level.md). Its main interface is the call stack's own
// (dmr/call/dmr_call.h); this adds what only a call-level chip has, for the debug link
#ifndef RFOUNDRY_DMR_CALL_LEVEL_H
#define RFOUNDRY_DMR_CALL_LEVEL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	bool running;		// on a DMR channel: the chip is powered
	bool ready;		// the chip reported ready
	uint32_t version;	// its firmware version, as the driver reads it (0 unknown)
	uint8_t fault;		// the driver's last fault code, 0 none
	uint32_t faults;
	uint32_t push_fails;	// channel set-ups the driver didn't take (queue full), sent again
	uint8_t ber_pct;
	// The key in use (never its bytes): number, dmrc_key_type_t, the chip took it
	uint8_t key_number;
	uint8_t key_type;
	bool key_loaded;
} dmrcl_info_t;

const dmrcl_info_t *dmrcl_info(void);

#endif
