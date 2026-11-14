// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RT-4D call-level DMR (docs/spec/hardware-rt4d.md 5, 7.2.1): the HF6853 on USART3 (PA6 / PA7, 115200 8N1) with its
// CEN on PB1, driven by drivers/dmr/hf6853. The bus and audio hand-over is set_mode()'s (radio_rt4d.c), which the core
// calls before it powers the chip and after it powers it off; the driver opens the receive audio for each call heard
// (rt4d_dmr_audio). The key store is the stock application's own records in the data flash (5.4.1), read only: keys
// entered with the stock CPS work as they do on stock.
#include <string.h>
#include "config.h"
#include "rt4d.h"
#include "pins.h"
#include "platform.h"

#if CONFIG_DMR_CALL
#include "dmr/hf6853/hf6853.h"

#define UART_MUX_RX	8u	// pins.h
#define UART_MUX_TX	7u
#define RX_RING		256u	// 22 ms at 115200: more than the radio task's 10 ms between polls

// The stock key records (5.4.1): record n at KEY_BASE + (n - 1) * 48, byte 1 the type, the key from byte 16. How many
// records the stock application has is open (7.3); this many are read
#define KEY_BASE	0x82000u
#define KEY_RECORD	48u
#define KEY_OFF_TYPE	1u
#define KEY_OFF_BYTES	16u
#ifndef RT4D_DMR_KEYS
#define RT4D_DMR_KEYS	16u
#endif

static uint8_t rx_ring[RX_RING];

static const at32_aux_uart_cfg_t uart = {
	.usart = USART3,
	.irq = USART3_IRQn,
	.baud = 115200u,
	.rx = rx_ring,
	.rx_size = RX_RING,
};

static void enable(bool on)
{
	PIN_OUT(PIN_DIG_POWER, on);
}

static const hf6853_io_t io = {
	.enable = enable,
	.read = at32_aux_uart_read,
	.write = at32_aux_uart_write,
	.busy = at32_aux_uart_tx_busy,
	.rx_audio = rt4d_dmr_audio,
};

static void d_init(void)
{
	at32_pin_mux(PIN_PORT(PIN_DIG_TXD), PIN_MASK(PIN_DIG_TXD), UART_MUX_TX, false, GPIO_PULL_NONE);
	at32_pin_mux(PIN_PORT(PIN_DIG_RXD), PIN_MASK(PIN_DIG_RXD), UART_MUX_RX, false, GPIO_PULL_UP);
	at32_aux_uart_start(&uart);
	hf6853_init(&io);
}

// The stock types (0 ARC, 1 AES-128, 2 AES-256) to ours; anything else is an empty record
static uint8_t key_type(uint8_t stock)
{
	return stock == 0 ? DMRC_KEY_ARC : stock == 1 ? DMRC_KEY_AES128 : stock == 2 ? DMRC_KEY_AES256 : DMRC_KEY_OFF;
}

static bool key_get(uint8_t number, dmrc_key_t *out)
{
	if (number == 0 || number > RT4D_DMR_KEYS) {
		return false;
	}
	uint32_t at = KEY_BASE + (uint32_t)(number - 1u) * KEY_RECORD;
	uint8_t t;
	if (!rt4d_flash.read(at + KEY_OFF_TYPE, &t, 1)) {
		return false;
	}
	out->number = number;
	out->type = key_type(t);
	uint8_t n = dmrc_key_len(out->type);
	if (n == 0 || !rt4d_flash.read(at + KEY_OFF_BYTES, out->bytes, n)) {
		return false;
	}
	bool blank = true, zero = true;
	for (uint8_t i = 0; i < n; i++) {
		blank = blank && out->bytes[i] == 0xFFu;
		zero = zero && out->bytes[i] == 0;
	}
	return !blank && !zero;	// an erased or cleared record (type 0 is ARC, so a zeroed one would read as a key)
}

const hal_dmr_call_t rt4d_dmr_call = {
	.caps = HF6853_CAPS,
	.init = d_init,
	.power = hf6853_power,
	.is_ready = hf6853_is_ready,
	.poll = hf6853_poll,
	.listen = hf6853_listen,
	.channel = hf6853_channel,
	.groups = hf6853_groups,
	.call = hf6853_call,
	.key = hf6853_key,
	.key_count = RT4D_DMR_KEYS,
	.key_get = key_get,	// no key_put: the records sit below the flash glue's write floor (board.c), which keeps
				// the stock application's data as it is. Keys are entered with the stock CPS
#if CONFIG_DEBUGLINK
	.view_line = hf6853_view_line,
#endif
};

#if CONFIG_DEBUGLINK
// The driver's lines again in the board section, as dmrc.<key>: the dmr_stats section keeps only its first ones
bool rt4d_dmr_view_line(uint8_t i, char *buf, uint8_t len)
{
	static const char prefix[] = "dmrc.";
	if (len <= sizeof(prefix)) {
		return false;
	}
	memcpy(buf, prefix, sizeof(prefix) - 1u);
	return hf6853_view_line(i, buf + sizeof(prefix) - 1u, (uint8_t)(len - (sizeof(prefix) - 1u)));
}
#endif
#endif
