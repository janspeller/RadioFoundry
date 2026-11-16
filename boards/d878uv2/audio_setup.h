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
#ifndef D878UV2_MIC_PGA
#define D878UV2_MIC_PGA		0x14u	// unmuted, 10 dB
#endif
#define CODEC_MICPGA_DEFAULT	D878UV2_MIC_PGA
#define CODEC_HPL_DEFAULT	0x0Au	// unmuted, +10 dB

#define CODEC_LOL_GAIN		0x12u	// LOL Driver Gain: the line outputs feed the RF chip's modulator input
#define CODEC_LOR_GAIN		0x13u
#ifndef D878UV2_LINE_GAIN
#define D878UV2_LINE_GAIN	0x00u	// unmuted, 0 dB
#endif
#define CODEC_LINE_DEFAULT	D878UV2_LINE_GAIN

// The first two writes are also the documented microphone-enable sequence at TX entry.
#define CODEC_MIC_COUNT 2u
static sct_codec_write_t codec_writes[] = {
	{ CODEC_PAGE_OUT, 0x34, 0x80 },	// microphone on IN1L; gain alone does not connect it
	{ CODEC_PAGE_OUT, CODEC_MICPGA_L, CODEC_MICPGA_DEFAULT },	// the microphone
	{ CODEC_PAGE_OUT, CODEC_MICPGA_R, CODEC_MICPGA_DEFAULT },	// the RF chip into the codec
	{ CODEC_PAGE_OUT, CODEC_HPL_GAIN, CODEC_HPL_DEFAULT },		// the speaker, unmuted
	// Transmit audio: the codec's line outputs drive the RF chip's modulator input, so a transmission with these
	// muted puts a carrier on the air with nothing on it (hardware-d878uv2.md 4.6, inferred there)
	{ CODEC_PAGE_OUT, CODEC_LOL_GAIN, CODEC_LINE_DEFAULT },
	{ CODEC_PAGE_OUT, CODEC_LOR_GAIN, CODEC_LINE_DEFAULT },
};

// The shared mic-gain setting supplies a dB offset; the codec uses half-dB units, with mute kept clear.
static inline uint8_t codec_mic_pga(int8_t offset_db)
{
	return tlv320aic3204_mic_gain(CODEC_MICPGA_DEFAULT, offset_db);
}

static inline void codec_mic_offset(int8_t offset_db)
{
	codec_writes[1].value = codec_mic_pga(offset_db);
}

static const uint8_t unity_gain[] = { 0x08, 0x00 };
static const sct_field_t voice_fields[] = {
	{ SCT_F_DIGI_MIC_GAIN, sizeof(unity_gain), unity_gain },
	{ SCT_F_DIGI_SPK_GAIN, sizeof(unity_gain), unity_gain },
};

static const sct_voice_t voice = {
	.codec = 1,
	.vocoder = 4,
	.fields = voice_fields,
	.field_count = sizeof(voice_fields) / sizeof(voice_fields[0]),
	.codec_writes = codec_writes,
	.codec_count = sizeof(codec_writes) / sizeof(codec_writes[0]),
};

#endif
