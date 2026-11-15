// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS diagnostics page lines: the raw readings the bring-up needs
// (docs/testing/d878uv2-bringup.md). The keys line is the important one: the matrix map is empty, so this is how a
// bench session finds which printed key sits at which position without a build.
#include <stddef.h>
#include "d878uv2.h"
#include "pins.h"
#include "mcu_io.h"
#include "config.h"
#include "debuglink/dl_fmt.h"
#if CONFIG_DMR
#include "modem/sct/sct_dmr.h"
#endif

#if CONFIG_DIAG

#define DIAG_LINES	7u

typedef struct {
	char *p, *end;
} out_t;

static void put(out_t *o, const char *s)
{
	while (*s && o->p < o->end) {
		*o->p++ = *s++;
	}
}

static void put_u(out_t *o, uint32_t v)
{
	char t[11];
	int n = 0;

	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v && n < 10);
	while (n && o->p < o->end) {
		*o->p++ = t[--n];
	}
}

static void put_x(out_t *o, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0 && o->p < o->end; i--) {
		*o->p++ = "0123456789ABCDEF"[(v >> (4 * i)) & 0xF];
	}
}

#if CONFIG_DMR
// sct_boot_result_t in order (drivers/modem/sct/sct_host.h)
static const char *boot_name(uint8_t r)
{
	static const char *const names[] = { "ok", "no block", "bus", "no app", "silent", "no voice" };
	return r < sizeof(names) / sizeof(names[0]) ? names[r] : "?";
}
#endif

bool d878uv2_diag_line(uint8_t i, char *buf, uint8_t len)
{
	if (len == 0 || i >= DIAG_LINES) {
		return false;
	}
	out_t o = { buf, buf + len - 1 };
	switch (i) {
	case 0:
		// The baseband: its boot result, then what the engine has seen
		put(&o, "bb ");
#if CONFIG_DMR
		put(&o, boot_name((uint8_t)d878uv2_modem_boot_result()));
		{
			sct_dmr_stats_t st;

			sct_dmr_get_stats(&st);
			put(&o, " rx ");
			put_u(&o, st.rx_frames);
			put(&o, " fec ");
			put_u(&o, st.rx_fec);
			put(&o, " nak ");
			put_u(&o, st.naks);
		}
#else
		put(&o, "off");
#endif
		break;
	case 1: {
		// The key matrix as it was last scanned: one hex digit per drive line (bit 0 = the first sense line, low
		// = pressed), then the four direct inputs. Press a key, read the line, write the position down: that is
		// how the map in input_d878uv2.c gets filled (hardware-d878uv2.md 4.1, 4.3)
		uint8_t raw[8];

		d878uv2_key_raw(raw);
		put(&o, "keys ");
		for (unsigned r = 0; r < 8u; r++) {
			put_x(&o, raw[r], 1);
		}
		put(&o, " dir ");
		put_x(&o, d878uv2_key_direct(), 1);
		put(&o, " (ptt1 a2 b4 pd8)");
		break;
	}
	case 2:
		// The battery and the panel potentiometer
		put(&o, "batt ");
		put_u(&o, board.power->battery_mv());
		put(&o, " mV ");
		put_u(&o, board.power->battery_pct());
		put(&o, "% vol ");
		put_u(&o, (uint32_t)board.power->volume());
		break;
	case 3:
		// The NAND's identity, and whether the core's storage mounted on it
		put(&o, "nand ");
		if (d878uv2_nand_ready()) {
			put_x(&o, d878uv2_nand_id(), 4);
		} else {
			put(&o, "no");
		}
		break;
	case 4: {
		// The receiver, in the shape the generic health test reads, plus the RF chip's GPIO shadow: the band and
		// front-end switching lives there, so this says which path is enabled (hardware-d878uv2.md 4.3)
		radio_status_t st;

		board.radio->status(&st);
		put(&o, "rssi ");
		put_u(&o, (uint32_t)(int32_t)(st.rssi_dbm + 160));
		put(&o, " nz ");
		put_u(&o, st.noise);
		put(&o, st.carrier ? " sig" : " -");
		put(&o, " gpio ");
		put_x(&o, d878uv2_rf_gpio(), 4);
		break;
	}
	case 5:
		put(&o, "adc ");
		put_u(&o, mcu_adc_read(ADC_CH_UNKNOWN));
		put(&o, " ");
		put_u(&o, mcu_adc_read(ADC_CH_BATTERY));
		put(&o, " ");
		put_u(&o, mcu_adc_read(ADC_CH_VOLUME));
		break;
	case 6:
		put(&o, "power ");
		put_u(&o, mcu_pin_read(PIN_POWER_KEY));
		put(&o, " knob ");
		put_u(&o, mcu_pin_read(PIN_KNOB_A));
		put_u(&o, mcu_pin_read(PIN_KNOB_B));
		put(&o, " sense ");
		put_u(&o, mcu_pin_read(PIN_KEY_SENSE0));
		put_u(&o, mcu_pin_read(PIN_KEY_SENSE1));
		put_u(&o, mcu_pin_read(PIN_KEY_SENSE2));
		break;
	default:
		return false;
	}
	*o.p = '\0';
	return true;
}

// Stable raw view keys for host tests and interactive bring-up.
bool d878uv2_view_line(uint8_t i, char *buf, uint8_t len)
{
	if (len == 0 || i >= 14u) {
		return false;
	}
	if (i < 3u) {
		dl_fmt(buf, len, "adc.%u=%u", 10u + i, (unsigned)mcu_adc_read((uint8_t)(10u + i)));
	} else if (i == 3u) {
		dl_fmt(buf, len, "power_switch=%u", (unsigned)mcu_pin_read(PIN_POWER_KEY));
	} else if (i == 4u) {
		unsigned ab = (mcu_pin_read(PIN_KNOB_A) ? 1u : 0u) | (mcu_pin_read(PIN_KNOB_B) ? 2u : 0u);
		dl_fmt(buf, len, "knob_ab=%u", ab);
	} else if (i == 5u) {
		dl_fmt(buf, len, "matrix_idle=%u", (unsigned)d878uv2_key_idle());
	} else {
		uint8_t raw[8];
		d878uv2_key_raw(raw);
		dl_fmt(buf, len, "matrix.%u=%u", (unsigned)(i - 6u), (unsigned)raw[i - 6u]);
	}
	return true;
}

void d878uv2_diag_reset(void)
{
#if CONFIG_DMR
	sct_dmr_reset_stats();
#endif
}

#endif	// CONFIG_DIAG
