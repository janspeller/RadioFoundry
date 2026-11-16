// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// TLV320AIC3204 microphone PGA: 0..47.5 dB in half-dB steps; bit 7 mutes.
#ifndef TLV320AIC3204_H
#define TLV320AIC3204_H
#include <stdint.h>

static inline uint8_t tlv320aic3204_mic_gain(uint8_t baseline, int8_t offset_db)
{
	int gain = (int)(baseline & 0x7Fu) + (int)offset_db * 2;
	if (gain < 0) gain = 0;
	if (gain > 95) gain = 95;
	return (uint8_t)gain;
}
#endif
