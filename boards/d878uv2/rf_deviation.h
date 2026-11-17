// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef D878UV2_RF_DEVIATION_H
#define D878UV2_RF_DEVIATION_H
#include <stdint.h>
#include <stdbool.h>

// Frequency correction classes in 10 Hz units; exact frequencies override interval classes.
// Values and precedence: docs/spec/hardware-d878uv2.md, final calibrated register 59.
typedef struct { uint32_t low, high; uint8_t correction; } rf_deviation_range_t;
typedef struct { uint32_t frequency; uint8_t correction; } rf_deviation_exact_t;

static inline uint8_t rf_deviation_class(uint32_t hz)
{
	static const rf_deviation_range_t uhf[] = {
		{ 40050000u, 40070000u, 2 },
		{ 43230250u, 43330000u, 2 },
		{ 45490000u, 45499750u, 2 },
		{ 45500250u, 45530000u, 2 },
		{ 47126000u, 47135000u, 2 },
		{ 47198000u, 47206000u, 2 },
		{ 47207000u, 47220000u, 2 },
		{ 47358000u, 47380000u, 2 },
		{ 47430000u, 47465000u, 2 },
		{ 47510000u, 47550000u, 2 },
		{ 47600000u, 48000000u, 2 },
		{ 48004000u, 48040000u, 2 },
		{ 48100000u, 48115000u, 2 },
		{ 48160000u, 48200000u, 2 },
		{ 40850000u, 40880000u, 1 },
		{ 40910000u, 41000000u, 1 },
		{ 41020000u, 41040000u, 1 },
		{ 43115000u, 43230000u, 1 },
		{ 45470000u, 45489750u, 1 },
		{ 45570000u, 45590000u, 1 },
		{ 48425000u, 48435000u, 1 },
		{ 48490000u, 48506000u, 1 },
		{ 48507000u, 48520000u, 1 },
		{ 48820000u, 48842000u, 1 },
		{ 48908000u, 48920000u, 1 },
	};
	static const rf_deviation_range_t vhf[] = {
		{ 13620500u, 13622500u, 2 },
		{ 13623250u, 13624750u, 2 },
		{ 13642000u, 13649750u, 2 },
		{ 13650250u, 13659000u, 2 },
		{ 13675500u, 13676750u, 2 },
		{ 13677750u, 13678750u, 2 },
		{ 14398000u, 14407750u, 2 },
		{ 14408250u, 14418000u, 2 },
		{ 14433000u, 14435000u, 2 },
		{ 14436000u, 14437250u, 2 },
		{ 15817250u, 15819750u, 2 },
		{ 15841750u, 15843000u, 2 },
		{ 15844750u, 15845750u, 2 },
		{ 15893500u, 15951500u, 2 },
		{ 15952250u, 15956500u, 2 },
		{ 16034000u, 16035750u, 2 },
		{ 16656750u, 16659000u, 2 },
		{ 16677750u, 16682750u, 2 },
		{ 16683250u, 16691000u, 2 },
		{ 16708250u, 16710000u, 2 },
		{ 16711000u, 16712500u, 2 },
		{ 15160250u, 15166750u, 1 },
		{ 15167250u, 15178750u, 1 },
		{ 17331000u, 17332750u, 1 },
		{ 17333750u, 17336000u, 1 },
		{ 17358000u, 17359750u, 1 },
		{ 17361000u, 17362750u, 1 },
	};
	static const rf_deviation_exact_t uhf_exact[] = {
		{ 40950000u, 1 },
		{ 42087500u, 1 },
		{ 42087250u, 1 },
		{ 42087750u, 1 },
		{ 43225000u, 1 },
		{ 44362250u, 1 },
		{ 44362500u, 1 },
		{ 44362750u, 1 },
		{ 46637250u, 1 },
		{ 46637500u, 1 },
		{ 46637750u, 1 },
		{ 47774750u, 1 },
		{ 47775000u, 1 },
		{ 47775250u, 1 },
		{ 48912500u, 1 },
		{ 50050000u, 1 },
		{ 51187500u, 1 },
		{ 45500000u, 2 },
		{ 49400000u, 2 },
		{ 52000000u, 2 },
	};
	static const rf_deviation_exact_t vhf_exact[] = {
		{ 14028750u, 1 },
		{ 14029000u, 1 },
		{ 14029250u, 1 },
		{ 14029375u, 1 },
		{ 14029500u, 1 },
		{ 14408000u, 1 },
		{ 14408125u, 1 },
		{ 14787250u, 1 },
		{ 14787500u, 1 },
		{ 14787750u, 1 },
		{ 15167000u, 1 },
		{ 15166875u, 1 },
		{ 15545500u, 1 },
		{ 15545625u, 1 },
		{ 15545750u, 1 },
		{ 15546000u, 1 },
		{ 15546250u, 1 },
		{ 16303750u, 1 },
		{ 16304250u, 1 },
		{ 16304375u, 1 },
		{ 16304500u, 1 },
		{ 16304000u, 1 },
		{ 16683000u, 1 },
		{ 16683125u, 1 },
		{ 17062250u, 1 },
		{ 17062500u, 1 },
		{ 17062750u, 1 },
		{ 13650000u, 2 },
		{ 15925000u, 2 },
	};
	bool high = hz / 10u > 30000000u;
	const rf_deviation_range_t *ranges = high ? uhf : vhf;
	const rf_deviation_exact_t *exact = high ? uhf_exact : vhf_exact;
	unsigned nr = high ? sizeof(uhf) / sizeof(uhf[0]) : sizeof(vhf) / sizeof(vhf[0]);
	unsigned ne = high ? sizeof(uhf_exact) / sizeof(uhf_exact[0]) : sizeof(vhf_exact) / sizeof(vhf_exact[0]);
	uint32_t f = hz / 10u;
	uint8_t result = 0;
	for (unsigned i = 0; i < nr; i++) {
		if (f >= ranges[i].low && f <= ranges[i].high) result = ranges[i].correction;
	}
	for (unsigned i = 0; i < ne; i++) {
		if (f == exact[i].frequency) result = exact[i].correction;
	}
	return result;
}

static inline uint16_t rf_deviation_value(uint32_t hz, uint8_t voice, uint8_t ctcss, uint8_t dcs, bool use_dcs)
{
	uint8_t correction = rf_deviation_class(hz);
	if (correction != 0) {
		uint8_t increment = hz / 10u > 30000000u ? (correction == 1 ? 16u : 6u) : (correction == 1 ? 6u : 17u);
		voice = (uint8_t)(voice + increment);
	}
	// Byte wrapping and the unmasked subtone OR match the calibrated producer.
	return (uint16_t)(((uint16_t)voice << 6) | (use_dcs ? dcs : ctcss));
}
#endif
