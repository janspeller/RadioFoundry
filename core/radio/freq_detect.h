// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Frequency detect (CONFIG_FREQ_DETECT, docs/spec/freq-detect.md): find a nearby transmitter's frequency with the RF
// chip's frequency counter scan (CAP_FREQ_DETECT, hal_radio_t.fd_*). The state machine is pure: the radio core steps
// it at its poll with the board's hooks (radio_sm.c), the tests with fakes. Nothing here blocks.
#ifndef FREQ_DETECT_H
#define FREQ_DETECT_H

#include <stdint.h>
#include <stdbool.h>

// The chip is read every FDET_POLL_MS. After FDET_GIVE_UP_POLLS reads without a result the scan goes off and on
// again (spec 2); a result outside the range is dropped the same way
#define FDET_POLL_MS		50u
#define FDET_GIVE_UP_POLLS	10u

typedef enum { FDET_OFF, FDET_SEARCH } fdet_state_t;

// The board's side (hal_radio_t.fd_start, fd_poll, fd_stop)
typedef struct {
	bool (*start)(uint32_t lo_hz, uint32_t hi_hz);
	bool (*poll)(uint32_t *hz);
	void (*stop)(void);
} fdet_ops_t;

typedef struct {
	uint8_t state;		// fdet_state_t
	uint8_t range;		// the range searched (an index into hal_radio_t.fd_ranges)
	uint32_t lo_hz, hi_hz;
	uint8_t polls;		// reads without a result since the scan went on
	uint32_t next_ms;	// the next read
	uint32_t hz;		// the last result in the range, 0 = none since the start
	uint32_t hits;		// results in the range since the start
	uint32_t drops;		// results outside it
	uint32_t restarts;	// scans given up without a result
} fdet_t;

void fdet_init(fdet_t *d);
// Start searching [lo_hz, hi_hz] (range: its index, for the view). False when the board refused; d stays off
bool fdet_start(fdet_t *d, const fdet_ops_t *ops, uint8_t range, uint32_t lo_hz, uint32_t hi_hz, uint32_t now_ms);
// Stop: the scan off. The last result stays readable
void fdet_stop(fdet_t *d, const fdet_ops_t *ops);
// Call at every radio poll while searching. True when a new result came (d->hz, d->hits changed)
bool fdet_step(fdet_t *d, const fdet_ops_t *ops, uint32_t now_ms);
// hz rounded to the nearest multiple of step_hz (step_hz 0: hz as it is)
uint32_t fdet_round(uint32_t hz, uint32_t step_hz);

#endif
