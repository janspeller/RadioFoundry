// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef D878UV2_AUDIO_SETUP_H
#define D878UV2_AUDIO_SETUP_H
#include "modem/sct/sct_host.h"
#include "audio/tlv320aic3204/tlv320aic3204.h"

// The codec gains the MCU sets after the baseband has booted. The baseband's own firmware sets the codec's
// clocks, routing and converters; the MCU only trims gains (docs/spec/hardware-d878uv2.md 4.6).
//
// These defaults are not the radio's saved audio setup: stock takes line/right-PGA gains from calibration,
// left-PGA gain from a microphone setting and HPL gain from its volume curve. What matters here is that the input is not left muted: the
// TLV320AIC3204 powers up with both MICPGAs muted (bit 7), and the right one carries the RF chip's audio into the
// codec, so a port that never writes it is silent however loud the output gain is. The output gain is set again
// on every volume change (radio_d878uv2.c)
#define CODEC_PAGE_OUT		1u
#define CODEC_MICPGA_R		0x3Cu	// Right MICPGA Volume Control: bit 7 mute, bits 6:0 in 0.5 dB steps
#define CODEC_MICPGA_L		0x3Bu	// Left MICPGA Volume Control: the microphone's side
#define CODEC_HPL_GAIN		0x10u	// HPL Driver Gain: bit 6 mute, bits 5:0 two's complement dB
// The microphone chain's gains are placeholders until the calibration's own audio bytes are identified, and a
// transmission that the other radio can barely hear means one of its stages is too low. Both can be moved at
// build time to find which: D878UV2_MIC_PGA is the microphone into the codec (0.5 dB steps, so 0x28 is 20 dB),
// D878UV2_LINE_GAIN the codec's line outputs into the RF chip's modulator (two's complement dB)
// The three per-radio gain bytes, with the factory defaults as the starting point. They are SMALL - 0x04, 0x08
// and 0x0C, which read as 2, 4 and 6 dB in the codec's half-dB steps - and the 0x14 (10 dB) this board guessed
// for the receive input was well above all of them, which is what a receiver that hisses sounds like. The
// radio's own values live in the calibration record at +0x0080 (+0x12, +0x13, +0x14)
// (docs/spec/hardware-d878uv2.md 4.6.6, 5.3)
#ifndef D878UV2_MIC_PGA
#define D878UV2_MIC_PGA		0x0Cu	// the right MICPGA: the RF chip into the codec
#endif
#define CODEC_MICPGA_DEFAULT	D878UV2_MIC_PGA
#define CODEC_LOL_DEFAULT	0x04u
#define CODEC_LOR_DEFAULT	0x08u
#define CODEC_HPL_DEFAULT	0x0Au	// unmuted, +10 dB

#define CODEC_LOL_GAIN		0x12u	// LOL Driver Gain: the line outputs feed the RF chip's modulator input
#define CODEC_LOR_GAIN		0x13u
#ifndef D878UV2_LINE_GAIN
#define D878UV2_LINE_GAIN	0x00u	// unmuted, 0 dB
#endif
#define CODEC_LINE_DEFAULT	D878UV2_LINE_GAIN

// The microphone's own gain is FOUR codec writes, not one (docs/spec/hardware-d878uv2.md 4.6): the routing, the
// PGA gain, and the AGC with its ceiling. Without the AGC the microphone runs at whatever the PGA alone gives,
// which is why a transmission was barely audible at the other radio. The stock firmware takes `g` from a
// five-entry table indexed by its Mic setting - 0x18, 0x1C, 0x20, 0x24, 0x2A - and writes:
//   page 1 reg 0x34 = 0x80   route IN1L into the left MICPGA (gain alone does not connect it)
//   page 1 reg 0x3B = g      the left MICPGA's gain
//   page 0 reg 0x56 = g|0x80 the AGC, enabled
//   page 0 reg 0x58 = g      the AGC's maximum gain
#define CODEC_PAGE_AGC		0u
#define CODEC_AGC_CTL1		0x56u
#define CODEC_AGC_CTL3		0x58u
#define CODEC_AGC_ON		0x80u
// The middle of the stock table; the Mic setting moves it (codec_mic_offset)
#ifndef D878UV2_MIC_GAIN
#define D878UV2_MIC_GAIN	0x20u
#endif

// The first four writes are the microphone-enable sequence at TX entry.
#define CODEC_MIC_COUNT 4u
static sct_codec_write_t codec_writes[] = {
	{ CODEC_PAGE_OUT, 0x34, 0x80 },	// microphone on IN1L; gain alone does not connect it
	{ CODEC_PAGE_OUT, CODEC_MICPGA_L, D878UV2_MIC_GAIN },		// the microphone's PGA gain
	{ CODEC_PAGE_AGC, CODEC_AGC_CTL1, D878UV2_MIC_GAIN | CODEC_AGC_ON },	// its AGC, on
	{ CODEC_PAGE_AGC, CODEC_AGC_CTL3, D878UV2_MIC_GAIN },		// the AGC's ceiling
	{ CODEC_PAGE_OUT, CODEC_MICPGA_R, CODEC_MICPGA_DEFAULT },	// the RF chip into the codec
	{ CODEC_PAGE_OUT, CODEC_HPL_GAIN, CODEC_HPL_DEFAULT },		// the speaker, unmuted
	// Transmit audio: the codec's line outputs drive the RF chip's modulator input, so a transmission with these
	// muted puts a carrier on the air with nothing on it (hardware-d878uv2.md 4.6, inferred there)
	{ CODEC_PAGE_OUT, CODEC_LOL_GAIN, CODEC_LOL_DEFAULT },
	{ CODEC_PAGE_OUT, CODEC_LOR_GAIN, CODEC_LOR_DEFAULT },
};
// Which row is which, so a value never lands on the wrong register when the table is reordered
#define CODEC_AT_MICPGA_L	1u
#define CODEC_AT_AGC1		2u
#define CODEC_AT_AGC3		3u
#define CODEC_AT_MICR		4u
#define CODEC_AT_HPL		5u
#define CODEC_AT_LOL		6u
#define CODEC_AT_LOR		7u
_Static_assert(sizeof(codec_writes) / sizeof(codec_writes[0]) == 8, "the indexes above follow the table");

// The shared mic-gain setting supplies a dB offset; the codec uses half-dB units, with mute kept clear.
static inline uint8_t codec_mic_pga(int8_t offset_db)
{
	return tlv320aic3204_mic_gain(D878UV2_MIC_GAIN, offset_db);
}

// The Mic setting moves all three gain writes together, the way the stock table's one byte feeds all of them
static inline void codec_mic_offset(int8_t offset_db)
{
	uint8_t g = codec_mic_pga(offset_db);

	codec_writes[CODEC_AT_MICPGA_L].value = g;
	codec_writes[CODEC_AT_AGC1].value = (uint8_t)(g | CODEC_AGC_ON);
	codec_writes[CODEC_AT_AGC3].value = g;
}

// The three 16-bit levels are the radio's own, not unity: the factory's are 0x0000, 0x0546 (1350) and 0x07D0
// (2000), and sending 2048 for the two digital gains was a guess that is wrong in both
// (docs/spec/hardware-d878uv2.md 4.6.6)
static uint8_t demod_gain_v[2] = { 0x00, 0x00 };
static uint8_t spk_gain_v[2] = { 0x05, 0x46 };
static uint8_t mic_gain_v[2] = { 0x07, 0xD0 };
static const sct_field_t voice_fields[] = {
	{ SCT_F_DEMOD_GAIN, sizeof(demod_gain_v), demod_gain_v },
	{ SCT_F_DIGI_MIC_GAIN, sizeof(mic_gain_v), mic_gain_v },
	{ SCT_F_DIGI_SPK_GAIN, sizeof(spk_gain_v), spk_gain_v },
};

// Take everything the calibration has for the audio chain, before the voice set-up goes out. The gain bytes go
// to the codec through the baseband; the three levels are baseband fields of their own
static inline void codec_audio_from_cal(void)
{
	d878uv2_cal_audio_t a;

	d878uv2_cal_audio(&a);
	codec_writes[CODEC_AT_LOL].value = a.lol;
	codec_writes[CODEC_AT_LOR].value = a.lor;
	codec_writes[CODEC_AT_MICR].value = a.mic_r;
	demod_gain_v[0] = (uint8_t)(a.demod >> 8);
	demod_gain_v[1] = (uint8_t)a.demod;
	spk_gain_v[0] = (uint8_t)(a.spk_gain >> 8);
	spk_gain_v[1] = (uint8_t)a.spk_gain;
	mic_gain_v[0] = (uint8_t)(a.mic_gain >> 8);
	mic_gain_v[1] = (uint8_t)a.mic_gain;
}

static const sct_voice_t voice = {
	.codec = 1,
	.vocoder = 4,
	.fields = voice_fields,
	.field_count = sizeof(voice_fields) / sizeof(voice_fields[0]),
	.codec_writes = codec_writes,
	.codec_count = sizeof(codec_writes) / sizeof(codec_writes[0]),
};

#endif
