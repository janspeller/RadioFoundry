// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The HF6853's command data (docs/spec/hardware-rt4d.md 5.3, 5.4.1, 5.4.2, 5.5), with exactly the bytes the pages give
#include <string.h>
#include "hf6853.h"
#include "hf6853_frame.h"

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

size_t hf6853_cmd_channel(uint8_t *out, const dmrc_channel_t *ch)
{
	memset(out, 0, HF6853_CHANNEL_LEN);
	out[0] = ch->ts == 2 ? 2u : 1u;
	out[1] = ch->cc & 0x0Fu;
	out[2] = 0;			// dual capacity direct mode: off (no codeplug field yet)
	put32(out + 3, ch->rx_hz);
	put32(out + 7, ch->tx_hz);
	out[11] = 4;
	out[12] = out[13] = out[14] = out[15] = 0xAAu;
	out[16] = 0;
	// 17, 18: not identified (5.4.1, 7.3), 0
	out[19] = ch->key_number;
	return HF6853_CHANNEL_LEN;
}

size_t hf6853_cmd_key(uint8_t *out, const dmrc_key_t *k)
{
	// The chip's type codes (5.4.1): 0 off, 1 ARC, 4 AES-128, 5 AES-256
	static const uint8_t type[] = { [DMRC_KEY_OFF] = 0, [DMRC_KEY_ARC] = 1, [DMRC_KEY_AES128] = 4,
					[DMRC_KEY_AES256] = 5 };
	uint8_t n = k != NULL && k->number != 0 && k->type < sizeof(type) ? dmrc_key_len(k->type) : 0;
	if (n == 0) {
		out[0] = out[1] = 0;
		return 2;
	}
	out[0] = k->number;
	out[1] = type[k->type];
	memcpy(out + 2, k->bytes, n);
	return 2u + n;
}

// id as 8 packed BCD digits into 4 bytes, the lowest two digits first
static void bcd_le(uint8_t *out, uint32_t id)
{
	if (id > 99999999u) {
		id = 99999999u;		// 8 digits; a DMR ID has at most 8
	}
	for (int i = 0; i < 4; i++) {
		uint8_t lo = (uint8_t)(id % 10u);
		id /= 10u;
		out[i] = (uint8_t)((id % 10u) << 4 | lo);
		id /= 10u;
	}
}

size_t hf6853_cmd_own_id(uint8_t *out, uint32_t id)
{
	bcd_le(out, id);
	return 4;
}

size_t hf6853_cmd_call(uint8_t *out, dmrc_call_type_t type, uint32_t id)
{
	if (type == DMRC_CALL_ALL) {
		out[0] = 4;
		out[1] = out[2] = out[3] = out[4] = 0xAAu;
		return HF6853_CALL_LEN;
	}
	out[0] = type == DMRC_CALL_PRIVATE ? 1u : 2u;
	uint8_t le[4];
	bcd_le(le, id);
	for (int i = 0; i < 4; i++) {
		out[1 + i] = le[3 - i];		// most significant first here
	}
	return HF6853_CALL_LEN;
}

// 4 bytes as packed BCD, most significant first; with a nibble over 9 the big-endian number, counted
static uint32_t id_from(const uint8_t *p, uint32_t *not_bcd)
{
	uint32_t bcd = 0, raw = 0;
	bool ok = true;
	for (int i = 0; i < 4; i++) {
		ok = ok && (p[i] >> 4) <= 9u && (p[i] & 0x0Fu) <= 9u;
		bcd = bcd * 100u + (uint32_t)(p[i] >> 4) * 10u + (p[i] & 0x0Fu);
		raw = raw << 8 | p[i];
	}
	if (!ok) {
		(*not_bcd)++;
		return raw;
	}
	return bcd;
}

bool hf6853_parse_heard(const uint8_t *data, uint16_t len, hf6853_heard_t *out, uint32_t *not_bcd)
{
	if (len < HF6853_HEARD_LEN) {
		return false;
	}
	out->type = data[0] == 1u ? DMRC_CALL_PRIVATE : data[0] == 4u ? DMRC_CALL_ALL : DMRC_CALL_GROUP;
	out->dst = out->type == DMRC_CALL_ALL ? DMRC_ID_ALL : id_from(data + 1, not_bcd);
	out->src = id_from(data + 5, not_bcd);
	return true;
}

const hf6853_step_t hf6853_startup[] = {
	{ 0x42u, 500, 1, 0xFFu },
	{ 0x3Eu, 500, 15, 0x0Fu },
	{ HF6853_CMD_PSAVE, 100, 1, 0x00u },
	{ 0x45u, 0, 1, 0x02u },
	{ 0x48u, 0, 1, 0x0Fu },
	{ 0x4Cu, 0, 1, 0x01u },
};
const uint8_t hf6853_startup_steps = sizeof(hf6853_startup) / sizeof(hf6853_startup[0]);

uint16_t hf6853_reply_ms(uint8_t cmd)
{
	switch (cmd) {
	case 0x06u:
		return 200;
	case 0x0Au:
	case 0x48u:
		return 2000;
	case 0x07u:
		return 4000;
	default:
		return 100;
	}
}

int16_t hf6853_rssi_dbm(uint8_t raw)
{
	int32_t dbm = HF6853_RSSI_DBM_LO + ((int32_t)raw - HF6853_RSSI_RAW_LO) * (HF6853_RSSI_DBM_HI - HF6853_RSSI_DBM_LO) /
		      (HF6853_RSSI_RAW_HI - HF6853_RSSI_RAW_LO);
	return (int16_t)(dbm < -160 ? -160 : dbm > 0 ? 0 : dbm);
}
