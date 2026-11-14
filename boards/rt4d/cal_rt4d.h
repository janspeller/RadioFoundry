// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D factory calibration (cal_rt4d.c, docs/spec/hardware-rt4d.md 3.2). Platform free, so the host tests build it
#ifndef CAL_RT4D_H
#define CAL_RT4D_H

#include <stdint.h>
#include <stdbool.h>
#include "hal_flash.h"

#define RT4D_CAL_BANDS		11u
#define RT4D_CAL_ENTRIES	16u
#define RT4D_CAL_BASE		0x20u	// band b's record at RT4D_CAL_BASE + b x RT4D_CAL_RECORD in the data flash
#define RT4D_CAL_RECORD		0x70u
#define RT4D_CAL_OFF_BIAS_HIGH	0x10u	// the stock power setting 1, probably high (3.2, inferred)
#define RT4D_CAL_OFF_BIAS_LOW	0x20u

// Read the records of the transmit bands (0 and 1) from the data flash. False: the flash didn't answer, or a record
// isn't plausible (erased): the radio then refuses to transmit
bool rt4d_cal_load(const hal_flash_t *flash);
bool rt4d_cal_valid(void);
// The calibration band of hz (0..10), -1 outside them all; uhf: the UHF path (from 240 MHz up)
int rt4d_cal_band(uint32_t hz, bool *uhf);
// The REG_38/39 value for hz (10 Hz units): the band's reference offset applied on a transmit band with a calibration
uint32_t rt4d_cal_freq_10hz(uint32_t hz);
// The REG_36 bias for hz at power level (0 low, 1 high); 0 when there is none
uint8_t rt4d_cal_bias(uint32_t hz, uint8_t level);
// Frequency detect (3.1.1): the transmitter's frequency in Hz from the BK4819's raw scan reading (10 Hz units) in the
// range that holds range_hz: raw + RT4D_DETECT_ADD - that band's reference offset. 0 when it comes out below 0
#define RT4D_DETECT_ADD		0x8028u
uint32_t rt4d_cal_detect_hz(uint32_t raw_10hz, uint32_t range_hz);

#endif
