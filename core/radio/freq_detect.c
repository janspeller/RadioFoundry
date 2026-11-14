// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Frequency detect's state machine (freq_detect.h, docs/spec/freq-detect.md)
#include "config.h"
#include "radio/freq_detect.h"

#if CONFIG_FREQ_DETECT

void fdet_init(fdet_t *d)
{
	*d = (fdet_t){ .state = FDET_OFF };
}

bool fdet_start(fdet_t *d, const fdet_ops_t *ops, uint8_t range, uint32_t lo_hz, uint32_t hi_hz, uint32_t now_ms)
{
	if (d->state != FDET_OFF) {
		fdet_stop(d, ops);
	}
	fdet_init(d);
	if (lo_hz > hi_hz || !ops->start(lo_hz, hi_hz)) {
		return false;
	}
	d->state = FDET_SEARCH;
	d->range = range;
	d->lo_hz = lo_hz;
	d->hi_hz = hi_hz;
	d->next_ms = now_ms + FDET_POLL_MS;
	return true;
}

void fdet_stop(fdet_t *d, const fdet_ops_t *ops)
{
	if (d->state == FDET_OFF) {
		return;
	}
	ops->stop();
	d->state = FDET_OFF;
}

// The scan off and on again: the chip measures afresh
static void restart(fdet_t *d, const fdet_ops_t *ops)
{
	ops->stop();
	d->polls = 0;
	if (!ops->start(d->lo_hz, d->hi_hz)) {
		d->state = FDET_OFF;
	}
}

bool fdet_step(fdet_t *d, const fdet_ops_t *ops, uint32_t now_ms)
{
	if (d->state != FDET_SEARCH || (int32_t)(now_ms - d->next_ms) < 0) {
		return false;
	}
	d->next_ms = now_ms + FDET_POLL_MS;
	uint32_t hz;
	if (!ops->poll(&hz)) {
		if (++d->polls >= FDET_GIVE_UP_POLLS) {
			d->restarts++;
			restart(d, ops);
		}
		return false;
	}
	bool in = hz >= d->lo_hz && hz <= d->hi_hz;
	if (in) {
		d->hz = hz;
		d->hits++;
	} else {
		d->drops++;
	}
	restart(d, ops);	// a result ends that scan (spec 2): the next one may find another transmitter
	return in;
}

uint32_t fdet_round(uint32_t hz, uint32_t step_hz)
{
	if (step_hz == 0u) {
		return hz;
	}
	return (hz + step_hz / 2u) / step_hz * step_hz;
}

#endif
