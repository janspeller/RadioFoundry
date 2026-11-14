// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D analog FM on the BK4819 (docs/spec/hardware-rt4d.md 3, 3.1, 5.5). The chip's 3-wire bus is bit-banged on
// PB7-PB9 (rfbus_3w). The HF6853 shares the bus: set_mode(DMR) hands it and the audio paths to the HF6853 (5.5), and
// until set_mode(FM) takes them back every FM hook leaves the BK4819 alone. Band paths and the PA are keyed through
// the BK4819's own GPIO outputs (3.1): GPIO2 / GPIO5 the UHF / VHF receive path, GPIO1 / GPIO4 the UHF / VHF transmit
// path, GPIO3 the APC, and PA10 routes the APC output to the PA in use. The transmit power is the calibration's REG_36 bias (cal_rt4d.c); without a calibration the
// radio refuses to transmit.
#include "rt4d.h"
#include "config.h"
#include "pins.h"
#include "platform.h"
#include "hal_time.h"
#include "rf/rfbus/rfbus.h"
#include "rf/bk4819/bk4819.h"
#include "debuglink/dl_fmt.h"
#if CONFIG_DTMF
#include "hal_dtmf.h"
#endif
#if CONFIG_DMR_CALL
#include "dmr/hf6853/hf6853.h"
#endif

// BK4819 GPIO outputs (3.1)
#define GPIO_U_TX	1u
#define GPIO_U_RX	2u
#define GPIO_APC	3u
#define GPIO_V_TX	4u
#define GPIO_V_RX	5u

// The stock values of 3.1
#define REG_POWER_RX	0x1F0Fu		// REG_37 in receive
#define REG_PA_ON	0x00FFu		// REG_36 = bias << 10 | 0x80 | 0x7F
// The TX state after 0xC1FE. The stock transmit ends on 0xC3FA (3.1), but in the shared driver's reading REG_30 bit 2
// is the microphone ADC and 0xC3FA has it off (bk4819.h, BK4819_STATE_TX_TONE). An image with 0xC3FA here sent a
// carrier without microphone audio (measured, 2026-11-11; PB10 was low, the microphone on the BK4819). So the default
// is the driver's main-chip TX state with the microphone ADC on, 0xC3FE; RT4D_DEFS="-DRT4D_TX_STATE=0xC3FAu" builds
// the stock value for comparison
#ifndef RT4D_TX_STATE
#define RT4D_TX_STATE	BK4819_STATE_TX_MAIN
#endif

// --- The 3-wire bus ---

static bool data_is_out;

static void bus_cs(bool level)
{
	PIN_WRITE(PIN_RF_CS, level);
}

static void bus_clk(bool level)
{
	PIN_WRITE(PIN_RF_CLK, level);
}

static void bus_data_out(bool level)
{
	PIN_WRITE(PIN_RF_DATA, level);
	if (!data_is_out) {
		PIN_OUT(PIN_RF_DATA, level);
		data_is_out = true;
	}
}

static void bus_data_release(void)
{
	PIN_IN(PIN_RF_DATA, GPIO_PULL_NONE);
	data_is_out = false;
}

static bool bus_data_in(void)
{
	return PIN_READ(PIN_RF_DATA);
}

static uint32_t bus_crit_enter(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

static void bus_crit_exit(uint32_t saved)
{
	__set_PRIMASK(saved);
}

static const rfbus_3w_pins_t bus_pins = {
	.cs = bus_cs,
	.clk = bus_clk,
	.data_out = bus_data_out,
	.data_release = bus_data_release,
	.data_in = bus_data_in,
	.delay_us = hal_delay_us,
	.crit_enter = bus_crit_enter,
	.crit_exit = bus_crit_exit,
	.delay_ms = hal_delay_ms,
};

static rfbus_3w_t bus3w;
static bk4819_t bk;

#define RT4D_AF_MUTE	0x6040u
#define RT4D_AF_GAIN	0xB32Au

// --- State ---

static bool supported;		// the chip answered with its ID at init
static bool dmr_bus;		// the HF6853 holds the bus and the audio (set_mode(DMR))
static bool transmitting;
static bool audio_open;
static bool dmr_audio;		// DMR: the HF6853's call heard plays (rt4d_dmr_audio)
static uint32_t rx_hz;
static radio_bw_t rx_bw;
static subtone_t rx_tone;

static const rfbus_regval_t init_table[] = {
	{ BK4819_REG_POWER, BK4819_POWER_ON },
	{ BK4819_REG_GPIO, 0x0100 },
	{ 0x34, 0x0000 }, { 0x35, 0x0000 },
	{ 0x1E, 0x4C58 }, { 0x1F, 0xA656 }, { 0x3E, 0xA037 }, { 0x7D, 0xE950 }, { 0x26, 0x13A0 },
	// AGC
	{ 0x13, 0x03BE }, { 0x12, 0x037B }, { 0x11, 0x027B }, { 0x10, 0x007A }, { 0x14, 0x0019 }, { 0x49, 0x2A38 },
	{ 0x7B, 0x8420 },
};

static bk4819_tone_t bk_tone(uint8_t type)
{
	switch (type) {
	case TONE_CTCSS:
		return BK4819_TONE_CTCSS;
	case TONE_DCS:
		return BK4819_TONE_DCS;
	case TONE_DCS_INV:
		return BK4819_TONE_DCS_INV;
	default:
		return BK4819_TONE_NONE;
	}
}

// Idle, then REG_38/39 with the calibration's reference offset (3.2)
static void tune(uint32_t hz)
{
	uint32_t f = rt4d_cal_freq_10hz(hz);
	bk4819_write(&bk, BK4819_REG_ENABLES, BK4819_STATE_IDLE_MAIN);
	bk4819_write(&bk, BK4819_REG_FREQ_LO, (uint16_t)(f & 0xFFFFu));
	bk4819_write(&bk, BK4819_REG_FREQ_HI, (uint16_t)(f >> 16));
	bk.freq_10hz = f;
}

// REG_47 is FM audio with the squelch open and in transmit, as the stock application sets it (3.1, Audio); the
// amplifier is on only for receive audio. In DMR it follows the call heard alone (5.4.2): off between calls and in
// transmit, where the HF6853's output carries the 4FSK buzz. The radio has no beeps or tones in DMR to share it with
static void speaker(void)
{
	if (dmr_bus) {
		PIN_WRITE(PIN_AMP_EN, dmr_audio);
		return;
	}
	bool on = audio_open && !transmitting && supported;
	if (supported) {
		bk4819_set_af(&bk, on || transmitting ? BK4819_AF_FM : bk.af_mute);
	}
	PIN_WRITE(PIN_AMP_EN, on);
}

// The bus to the MCU and the chip set up (5.5): the bus pins as outputs, 50 ms, the BK4819 init (3.1), then the
// modulation from the MCU side and the receive audio from the BK4819
static void init(void)
{
	PIN_OUT(PIN_DIG_POWER, false);		// the HF6853 stays off (5.5)
	PIN_OUT(PIN_AMP_EN, false);
	PIN_OUT(PIN_UV_APC, false);
	PIN_OUT(PIN_RF_CS, true);
	PIN_OUT(PIN_RF_CLK, false);
	PIN_OUT(PIN_RF_DATA, false);
	data_is_out = true;
	hal_delay_ms(50);

	rfbus_3w_attach(&bus3w, &bus_pins);
	bk4819_attach(&bk, &bus3w.bus);
	// The stock audio words (3.1, Audio): 0x47 = 0x6040 muted (0x6140 FM, the driver's), 0x48 = 0xB32A, written by
	// every analog mode set. Without 0x48 the speaker stays silent with the squelch open
	bk.af_mute = RT4D_AF_MUTE;
	bk.af_gain_analog = RT4D_AF_GAIN;
	bk4819_write(&bk, BK4819_REG_ID, 0x8000);
	bk4819_write(&bk, BK4819_REG_ID, 0x0000);
	hal_delay_ms(1);
	for (unsigned i = 0; i < sizeof(init_table) / sizeof(init_table[0]); i++) {
		bk4819_write(&bk, init_table[i].reg, init_table[i].val);
	}
	bk.idle = BK4819_STATE_IDLE_MAIN;
	supported = bk4819_read(&bk, BK4819_REG_ID) == BK4819_ID;
	if (supported) {
		// The shared driver's analog set (the BK4819's stock FM values on the other radio with it), with this radio's
		// mute word and AF gain (3.1, Audio)
		bk4819_analog_mode(&bk);
		bk4819_set_bandwidth(&bk, BK4819_BW_12K5);
	}

	PIN_OUT(PIN_MOD_SW, false);		// modulation from the MCU side
	PIN_OUT(PIN_AF_SW, false);		// receive audio from the BK4819 (3, Receive audio: PB10 and PF8 low)
	audio_open = false;
	transmitting = false;
	speaker();
}

static void tx_off(void);

// The HF6853 driver's speaker hook (hf6853_io_t.rx_audio, dmr_rt4d.c): a call heard opens the HF6853's audio, PF8
// high to the AUDIO net, then the amplifier; its end closes them in the reverse order (3, Receive audio; 5.4.2)
void rt4d_dmr_audio(bool on)
{
	dmr_audio = on;
	if (!dmr_bus) {
		return;
	}
	if (on) {
		PIN_WRITE(PIN_AF_SW, true);
		speaker();
	} else {
		speaker();
		PIN_WRITE(PIN_AF_SW, false);
	}
}

// DMR: the bus to the HF6853 (5.5), before the chip is powered (dmr_rt4d.c). FM: the bus back and the BK4819 set up
// again, after the chip is off; the core's rx() then tunes it
static void set_mode(radio_mode_t mode)
{
	if (!CONFIG_DMR_CALL || (mode == RADIO_MODE_DMR) == dmr_bus) {
		return;
	}
	if (mode == RADIO_MODE_DMR) {
		if (transmitting) {
			tx_off();
		}
		if (supported) {
			bk4819_set_af(&bk, bk.af_mute);
		}
		dmr_audio = false;
		PIN_WRITE(PIN_AMP_EN, false);
		PIN_WRITE(PIN_AF_SW, false);	// no receive audio until a call heard opens it (5.5)
		hal_delay_ms(10);
		PIN_IN(PIN_RF_CS, GPIO_PULL_NONE);
		PIN_IN(PIN_RF_CLK, GPIO_PULL_NONE);
		PIN_IN(PIN_RF_DATA, GPIO_PULL_NONE);
		data_is_out = false;
		PIN_WRITE(PIN_MOD_SW, true);	// modulation from the HF6853
		dmr_bus = true;
		speaker();
	} else {
		dmr_bus = false;
		dmr_audio = false;
		init();
	}
}

static void paths_off(void)
{
	bk4819_gpio(&bk, GPIO_APC, false);
	bk4819_gpio(&bk, GPIO_U_TX, false);
	bk4819_gpio(&bk, GPIO_V_TX, false);
}

static void start_rx(void)
{
	bool uhf;
	(void)rt4d_cal_band(rx_hz, &uhf);
	paths_off();
	bk4819_gpio(&bk, GPIO_U_RX, uhf);
	bk4819_gpio(&bk, GPIO_V_RX, !uhf);
	bk4819_write(&bk, BK4819_REG_POWER, REG_POWER_RX);
	tune(rx_hz);
	bk4819_set_bandwidth(&bk, rx_bw == RADIO_BW_25K ? BK4819_BW_25K : BK4819_BW_12K5);
	bk4819_set_rx_tone(&bk, bk_tone(rx_tone.type), rx_tone.value);
	bk4819_set_state(&bk, BK4819_STATE_RX);
}

static bool rx(uint32_t hz, radio_bw_t bw, const subtone_t *tone)
{
	if (!board_in_band(hz, false)) {
		return false;
	}
	rx_hz = hz;
	rx_bw = bw;
	rx_tone = tone != NULL ? *tone : (subtone_t){ TONE_NONE, 0 };
	if (supported && !transmitting && !dmr_bus) {
		start_rx();
	}
	return true;
}

// 3.1 Transmit: receive paths off, the frequency, the TX state, then the band's TX path with PA10, the APC and the
// power level's bias
static bool tx(uint32_t hz, radio_bw_t bw, uint8_t power, const subtone_t *tone)
{
	uint8_t bias = rt4d_cal_bias(hz, power);
	if (!supported || dmr_bus || !rt4d_cal_valid() || !board_in_band(hz, true) || bias == 0u) {
		return false;
	}
	bool uhf;
	(void)rt4d_cal_band(hz, &uhf);
	transmitting = true;
	speaker();
	bk4819_gpio(&bk, GPIO_U_RX, false);
	bk4819_gpio(&bk, GPIO_V_RX, false);
	bk4819_set_bandwidth(&bk, bw == RADIO_BW_25K ? BK4819_BW_25K : BK4819_BW_12K5);
	bk4819_tx_prepare(&bk, bk_tone(tone != NULL ? tone->type : TONE_NONE), tone != NULL ? tone->value : 0);
	tune(hz);
	bk4819_write(&bk, BK4819_REG_POWER, BK4819_POWER_ON);
	bk4819_write(&bk, BK4819_REG_CSS_DETECT, 0x028F);
	bk4819_write(&bk, BK4819_REG_ENABLES, BK4819_STATE_IDLE_MAIN);
	bk4819_write(&bk, BK4819_REG_ENABLES, BK4819_STATE_TX);
	bk4819_write(&bk, BK4819_REG_ENABLES, RT4D_TX_STATE);
	PIN_WRITE(PIN_UV_APC, !uhf);
	bk4819_gpio(&bk, uhf ? GPIO_U_TX : GPIO_V_TX, true);
	bk4819_gpio(&bk, GPIO_APC, true);
	bk4819_write(&bk, BK4819_REG_PA, (uint16_t)((uint16_t)bias << 10 | REG_PA_ON));
	return true;
}

static void tx_off(void)
{
	if (!supported || dmr_bus) {
		transmitting = false;
		return;
	}
	paths_off();
	bk4819_write(&bk, BK4819_REG_PA, 0x0000);
	bk4819_tx_end(&bk);
	transmitting = false;
	if (rx_hz != 0u) {
		start_rx();
	}
	speaker();
}

static bool tx_tail(bool on)
{
	if (!supported || !transmitting || dmr_bus) {
		return false;
	}
	if (on) {
		return bk4819_tx_tail_start(&bk);
	}
	bk4819_tx_tail_end(&bk);
	return true;
}

#if CONFIG_FM_TX_TONES || CONFIG_DTMF
// FM TX tones (1750 Hz burst, roger beep, CW ID) and DTMF: the shared driver's tone generator instead of the
// microphone (bk4819.h). FM transmit only: in DMR the HF6853 holds the bus. Untested on this radio
#ifndef RT4D_TX_TONE_GAIN
#define RT4D_TX_TONE_GAIN	BK4819_TONE_GAIN_STOCK
#endif

static bool tone_ok(void)
{
	return supported && transmitting && !dmr_bus;
}

// After a tone: the driver's stop goes back to its main TX state (0xC3FE) and the muted AF word; the board's own TX
// state (RT4D_TX_STATE) and AF follow
static void tone_done(void)
{
	if (RT4D_TX_STATE != BK4819_STATE_TX_MAIN) {
		bk4819_write(&bk, BK4819_REG_ENABLES, RT4D_TX_STATE);
	}
	speaker();
}
#endif

#if CONFIG_FM_TX_TONES
static bool tx_tone(uint16_t hz)
{
	if (!tone_ok()) {
		return false;
	}
	bk4819_tone_start(&bk, hz, RT4D_TX_TONE_GAIN);
	if (hz == 0) {
		tone_done();
	}
	return true;
}

static bool tx_tone_key(bool down)
{
	if (!tone_ok() || bk.tone_hz == 0) {
		return false;
	}
	bk4819_tone_key(&bk, down);
	return true;
}
#endif

#if CONFIG_DTMF
// The stock DTMF sequence of the shared driver; dropped at the end of the transmission like a tone (bk4819_tx_end)
static bool tx_dtmf(char digit)
{
	uint16_t low = 0, high = 0;
	if (!tone_ok() || (digit != 0 && !dtmf_pair(digit, &low, &high))) {
		return false;
	}
	bk4819_dtmf_start(&bk, low, high);
	if (digit == 0) {
		tone_done();
	}
	return true;
}
#endif

static void status(radio_status_t *st)
{
	if (!supported || transmitting || dmr_bus) {
		st->rssi_dbm = -160;		// nothing heard
		st->noise = UINT8_MAX;
		st->carrier = false;
		st->tone_ok = false;
#if CONFIG_DMR_CALL
		// The BK4819 is the HF6853's now: the level comes from it (5.4.3), while it hears a signal
		if (dmr_bus && hf6853_rssi(&st->rssi_dbm)) {
			st->carrier = true;
		}
#endif
		return;
	}
	// At the chip's input: the front end's gain isn't taken out (no calibration for it is known)
	st->rssi_dbm = bk4819_rssi_dbm(bk4819_read(&bk, BK4819_REG_RSSI));
	st->noise = (uint8_t)(bk4819_read(&bk, BK4819_REG_NOISE) & 0x7Fu);
	st->carrier = false;
	st->tone_ok = bk4819_rx_tone_ok(&bk, bk_tone(rx_tone.type));
}

static void audio(bool on)
{
	audio_open = on;
	speaker();
}

bool rt4d_radio_transmitting(void)
{
	return transmitting;
}

// The debug link's CHIP_REG (board.chip_reg): chip 0 is the BK4819, page 0 only
bool rt4d_chip_reg(uint8_t chip, uint8_t page, uint8_t reg, uint32_t *val, bool write)
{
	if (chip != 0 || page != 0 || reg > 0x7Fu || (write && *val > 0xFFFFu) || bk.bus == NULL || dmr_bus) {
		return false;
	}
	if (write) {
		bk4819_write(&bk, reg, (uint16_t)*val);
	}
	*val = bk4819_read(&bk, reg);
	return true;
}

#if CONFIG_FREQ_DETECT
// Frequency detect (3.1.1): the stock "Detect Range" list, the calibration bands of 3.2 in order of frequency
static const fd_range_t fd_ranges[] = {
	{ 18000000u, 64000000u }, { 64000000u, 136000000u }, { 136000000u, 174000000u }, { 174000000u, 240000000u },
	{ 240000000u, 320000000u }, { 320000000u, 400000000u }, { 400000000u, 480000000u },
	{ 480000000u, 560000000u }, { 560000000u, 660000000u }, { 840000000u, 920000000u },
	{ 920000000u, 1000000000u },
};
static uint32_t fd_mid_hz;	// the middle of the range searched: its calibration band's reference offset

// The band's receive path (V-RX below 240 MHz, else U-RX), the chip in receive, then the scan on
static bool fd_start(uint32_t lo_hz, uint32_t hi_hz)
{
	if (!supported || transmitting || dmr_bus) {
		return false;
	}
	fd_mid_hz = lo_hz / 2u + hi_hz / 2u;
	bool uhf;
	(void)rt4d_cal_band(fd_mid_hz, &uhf);
	paths_off();
	bk4819_gpio(&bk, GPIO_U_RX, uhf);
	bk4819_gpio(&bk, GPIO_V_RX, !uhf);
	if (bk.state != BK4819_STATE_RX) {	// never tuned yet: the receiver on as start_rx() has it
		bk4819_write(&bk, BK4819_REG_POWER, REG_POWER_RX);
		bk4819_set_state(&bk, BK4819_STATE_RX);
	}
	bk4819_freq_scan_start(&bk);
	return true;
}

static bool fd_poll(uint32_t *hz)
{
	uint32_t raw;
	if (!bk4819_freq_scan_poll(&bk, &raw)) {
		return false;
	}
	*hz = rt4d_cal_detect_hz(raw, fd_mid_hz);
	return true;
}

static void fd_stop(void)
{
	bk4819_freq_scan_stop(&bk);
}
#endif

const hal_radio_t rt4d_radio = {
	.init = init,
	.set_mode = set_mode,
	.rx = rx,
	.tx = tx,
	.tx_off = tx_off,
	.tx_tail = tx_tail,
	.status = status,
	.audio = audio,
#if CONFIG_FM_TX_TONES
	.tx_tone = tx_tone,
	.tx_tone_key = tx_tone_key,
#endif
#if CONFIG_DTMF
	.tx_dtmf = tx_dtmf,
#endif
#if CONFIG_FREQ_DETECT
	.fd_ranges = fd_ranges,
	.fd_range_count = sizeof(fd_ranges) / sizeof(fd_ranges[0]),
	.fd_start = fd_start,
	.fd_poll = fd_poll,
	.fd_stop = fd_stop,
#endif
};

#if CONFIG_DEBUGLINK
// The board's debug link view keys (board.view_line, hardware-rt4d.md 6.3): the audio path's state, read live. The
// registers come from the chip (not the driver's copies), so a write the code never made shows; "none" without a chip,
// "dmr" while the HF6853 holds the bus (no read then). The HF6853 driver's keys follow
bool rt4d_view_line(uint8_t i, char *buf, uint8_t len)
{
	static const uint8_t regs[] = { BK4819_REG_AF, BK4819_REG_AF_GAIN, BK4819_REG_ENABLES, BK4819_REG_POWER };
	if (i < sizeof(regs)) {
		if (dmr_bus) {
			dl_fmt(buf, len, "bk4819.reg%02x=dmr", regs[i]);
		} else if (supported) {
			dl_fmt(buf, len, "bk4819.reg%02x=0x%04x", regs[i], bk4819_read(&bk, regs[i]));
		} else {
			dl_fmt(buf, len, "bk4819.reg%02x=none", regs[i]);
		}
		return true;
	}
	switch (i - sizeof(regs)) {
	case 0:
		dl_fmt(buf, len, "amp_en=%u", PIN_READ(PIN_AMP_EN) ? 1u : 0u);
		return true;
	case 1:
		dl_fmt(buf, len, "af_sw=%u", PIN_READ(PIN_AF_SW) ? 1u : 0u);	// 1: FM receive muted
		return true;
	case 2:
		dl_fmt(buf, len, "audio_open=%u", audio_open ? 1u : 0u);
		return true;
	case 3:
		dl_fmt(buf, len, "mod_sw=%u", PIN_READ(PIN_MOD_SW) ? 1u : 0u);	// 0: the BK4819 path (FM), 1: the HF6853
		return true;
	default:
#if CONFIG_DMR_CALL
		return rt4d_dmr_view_line((uint8_t)(i - sizeof(regs) - 4u), buf, len);
#else
		return false;
#endif
	}
}
#endif

// --- Transmitter off from any context (board.h board_rf_safe) ---

// About 1 us a half bit at 72 MHz, longer on the reset clock: slower than the bus needs, never faster
static void safe_spin(void)
{
	for (volatile uint32_t i = 0; i < 24u; i++) {
	}
}

static void safe_frame(uint8_t reg, uint16_t val)
{
	uint32_t v = (uint32_t)(reg & 0x7Fu) << 16 | val;
	GPIOB->SCR = (uint32_t)PIN_MASK(PIN_RF_CLK) << 16;
	GPIOB->SCR = (uint32_t)PIN_MASK(PIN_RF_CS) << 16;
	for (int i = 23; i >= 0; i--) {
		GPIOB->SCR = (uint32_t)PIN_MASK(PIN_RF_CLK) << 16;
		GPIOB->SCR = ((v >> i) & 1u) ? PIN_MASK(PIN_RF_DATA) : (uint32_t)PIN_MASK(PIN_RF_DATA) << 16;
		safe_spin();
		GPIOB->SCR = PIN_MASK(PIN_RF_CLK);
		safe_spin();
	}
	GPIOB->SCR = (uint32_t)PIN_MASK(PIN_RF_CLK) << 16;
	GPIOB->SCR = PIN_MASK(PIN_RF_CS);
	safe_spin();
}

// The PA is keyed by the BK4819's GPIO outputs and its TX state (3.1), which survive an MCU reset, so plain pin writes
// can't stop it: two bus frames do, bit-banged with register writes and a counted spin (no timer, no RTOS). REG_33 = 0
// drops every chip GPIO (TX paths and the APC), REG_30 = 0 leaves the TX state. The HF6853 is powered down first so
// it neither transmits nor drives the bus; the APC route and the amplifier go to their idle levels
void board_rf_safe(void)
{
	PIN_FORCE(PIN_DIG_POWER, false);
	PIN_FORCE(PIN_AMP_EN, false);
	PIN_FORCE(PIN_UV_APC, false);
	PIN_FORCE(PIN_RF_CS, true);
	PIN_FORCE(PIN_RF_CLK, false);
	PIN_FORCE(PIN_RF_DATA, false);
	safe_frame(BK4819_REG_GPIO, 0x0000);
	safe_frame(BK4819_REG_ENABLES, 0x0000);
	data_is_out = true;
}
