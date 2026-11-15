// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS DMR baseband glue: the SCT3258's host port, its reset and boot, and the modem task
// (docs/spec/hardware-d878uv2.md 4.1, docs/spec/modem-sct.md).
//
// The chip runs DMR layers 1 and 2 and the AMBE+2 vocoder itself, and plays the voice to the codec without the
// MCU seeing it (CAP_MODEM_VOICE). The engine is drivers/modem/sct; this file is the board's part: the 8-bit
// parallel port on PD8-PD15 with its strobes on PD2-PD5, the reset sequence, and the 1024-word boot block the
// chip takes at every reset.
//
// The boot block is the radio maker's and never ships with RadioFoundry: it is merged from the owner's own stock
// firmware into the modem boot slot on their machine (tools/modem_boot.py, hardware-anytone.md 2.2). Without it
// the radio runs FM only and the boot reports "no block".
#include "d878uv2.h"
#include "config.h"
#if CONFIG_DMR	// the whole file
#include "pins.h"
#include "mcu_io.h"
#include "hal_time.h"
#include "modem/sct/sct_dmr.h"
#include "modem/sct/sct_host.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define MODEM_TASK_STACK	288
#define MODEM_TASK_PRIORITY	(configMAX_PRIORITIES - 2)
#define MODEM_POLL_MS		2
// The family's chip answers about 2.15 s after the boot block, so 2 s is too tight
#define BOOT_TIMEOUT_MS		5000
#define BOOT_POLL_MS		10
// CRC-32 (zlib) of the 2048-byte boot block every stock image in this family holds (SHA-256 743515b9...). The block
// is byte-identical across the family, so this is the same constant every SCT3258 board checks
#define BOOT_BLOCK_CRC32	0x024F600Au

extern const uint8_t __codec_slot[];

// The voice set-up sent once the application runs: the codec is a TLV320AIC3204 and the vocoder AMBE+2, the same
// pair the family's mobile carries (hardware-d878uv2.md 1.1). This board sends no gains or routing of its own, so
// the chip keeps its defaults; the per-radio audio values live in the NAND calibration, which has no reader yet
#define VOICE_CODEC		1
#define VOICE_VOCODER		4

// The bus has no wait line: a short settle inside each strobe. The stock firmware uses a 20-iteration busy loop
#define STROBE_US		1u
#define RESET_RUN_MS		100u
#define RESET_LOW_MS		500u

static inline void settle(void)
{
	hal_delay_us(STROBE_US);
}

// PD8-PD15: outputs only while a write runs, pull-up inputs between writes, as the stock firmware's whole-group
// direction change does (hardware-d878uv2.md 4.1)
static void data_dir(bool out)
{
	if (out) {
		mcu_port_high_byte_dir(HPI_DATA_PORT, true);
	} else {
		mcu_port_high_byte_pullup(HPI_DATA_PORT);
	}
}

// A write, per byte: CS low, WR low, the data, WR high, CS high. The chip latches on WR's rising edge
static bool hpi_write(const uint8_t *buf, uint16_t len, void *ctx)
{
	(void)ctx;
	data_dir(true);
	for (uint16_t i = 0; i < len; i++) {
		uint8_t v = buf[i];

		mcu_pin_write(PIN_HPI_CS, false);
		mcu_pin_write(PIN_HPI_WR, false);
		settle();
		mcu_port_put(HPI_DATA_PORT, (uint16_t)(v << HPI_DATA_SHIFT), (uint16_t)((uint8_t)~v << HPI_DATA_SHIFT));
		settle();
		mcu_pin_write(PIN_HPI_WR, true);
		mcu_pin_write(PIN_HPI_CS, true);
		settle();
	}
	data_dir(false);
	return true;
}

// A block read holds CS low and pulses RD per byte
static uint8_t read_byte(void)
{
	mcu_pin_write(PIN_HPI_RD, false);
	settle();
	uint8_t v = (uint8_t)(mcu_port_read(HPI_DATA_PORT) >> HPI_DATA_SHIFT);

	mcu_pin_write(PIN_HPI_RD, true);
	settle();
	return v;
}

// HOBIB low: the chip has a word waiting. Whole words only (16-bit mode, low byte first)
static uint16_t hpi_read(uint8_t *buf, uint16_t max, void *ctx)
{
	(void)ctx;
	uint16_t n = 0;

	if (mcu_pin_read(PIN_HPI_HOBI)) {
		return 0;
	}
	mcu_pin_write(PIN_HPI_CS, false);
	while (n + 2 <= max && !mcu_pin_read(PIN_HPI_HOBI)) {
		buf[n++] = read_byte();
		buf[n++] = read_byte();
	}
	mcu_pin_write(PIN_HPI_CS, true);
	return n;
}

// The reset sequence (hardware-d878uv2.md 4.1): the enables and strobes set, the strap low for Intel timing, RSTN
// high 100 ms then low 500 ms, then high with the strap released to a pulled-down input. PD1, PD7 and PC7 are
// driven through the same sequence; what they gate is not identified, so they are driven exactly as recorded
static void hpi_reset(void *ctx)
{
	(void)ctx;
	mcu_pin_out(PIN_SCT_EN0, true, false);
	mcu_pin_out(PIN_HPI_CS, true, false);
	mcu_pin_out(PIN_HPI_WR, true, false);
	mcu_pin_out(PIN_HPI_RD, true, false);
	mcu_pin_in(PIN_HPI_HOBI, MCU_PULL_UP);
	mcu_pin_out(PIN_SCT_AUX0, false, false);
	mcu_pin_out(PIN_SCT_AUX1, false, false);
	mcu_pin_out(PIN_SCT_STRAP, false, false);	// Intel timing
	mcu_pin_out(PIN_SCT_AUX2, false, false);
	mcu_pin_out(PIN_SCT_EN1, true, false);
	data_dir(false);

	mcu_pin_out(PIN_SCT_RESET, false, false);
	hal_delay_ms(RESET_RUN_MS);
	mcu_pin_write(PIN_SCT_RESET, true);
	hal_delay_ms(RESET_RUN_MS);
	mcu_pin_write(PIN_SCT_RESET, false);
	hal_delay_ms(RESET_LOW_MS);
	mcu_pin_write(PIN_SCT_RESET, true);
	// The strap is latched as the chip comes out of reset; it is then released to a pulled-down input
	mcu_pin_in(PIN_SCT_STRAP, MCU_PULL_DOWN);
}

// CRC-32 (zlib) of the slot's contents, to tell the boot block from an erased page or something else
static uint32_t crc32(const uint8_t *p, uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFu;

	while (len-- != 0) {
		crc ^= *p++;
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

// The codec gains the MCU sets after the baseband has booted. The baseband's own firmware sets the codec's
// clocks, routing and converters; the MCU only trims gains (docs/spec/hardware-d878uv2.md 4.6).
//
// **These values are placeholders**, not the radio's own: the stock firmware takes its five gain bytes from a
// calibration record this port does not read yet. What matters here is that the input is not left muted: the
// TLV320AIC3204 powers up with both MICPGAs muted (bit 7), and the right one carries the RF chip's audio into the
// codec, so a port that never writes it is silent however loud the output gain is. The output gain is set again
// on every volume change (radio_d878uv2.c)
#define CODEC_PAGE_OUT		1u
#define CODEC_MICPGA_R		0x3Cu	// Right MICPGA Volume Control: bit 7 mute, bits 6:0 in 0.5 dB steps
#define CODEC_MICPGA_L		0x3Bu	// Left MICPGA Volume Control: the microphone's side
#define CODEC_HPL_GAIN		0x10u	// HPL Driver Gain: bit 6 mute, bits 5:0 two's complement dB
#define CODEC_MICPGA_DEFAULT	0x14u	// unmuted, 10 dB
#define CODEC_HPL_DEFAULT	0x0Au	// unmuted, +10 dB

static const sct_codec_write_t codec_writes[] = {
	{ CODEC_PAGE_OUT, CODEC_MICPGA_R, CODEC_MICPGA_DEFAULT },	// the RF chip into the codec
	{ CODEC_PAGE_OUT, CODEC_MICPGA_L, CODEC_MICPGA_DEFAULT },	// the microphone
	{ CODEC_PAGE_OUT, CODEC_HPL_GAIN, CODEC_HPL_DEFAULT },		// the speaker, unmuted
};

static const sct_voice_t voice = {
	.codec = VOICE_CODEC,
	.vocoder = VOICE_VOCODER,
	.codec_writes = codec_writes,
	.codec_count = sizeof(codec_writes) / sizeof(codec_writes[0]),
};

static const sct_host_t host = {
	.write = hpi_write,
	.read = hpi_read,
	.reset = hpi_reset,
	.delay_ms = hal_delay_ms,
	.now_ms = hal_millis,
	.boot = SCT_BOOT_BLOCK,
	.boot_block = __codec_slot,
	.voice = &voice,
};

static SemaphoreHandle_t lock;
static bool booted, boot_done;
static sct_boot_result_t boot_result = SCT_BOOT_SILENT;

static bool port_send(const uint8_t *buf, uint16_t len, void *ctx)
{
	(void)ctx;
	return sct_host_send(&host, buf, len);
}

static bool port_ready(void *ctx)
{
	(void)ctx;
	return booted;
}

static void port_lock(void *ctx)
{
	(void)ctx;
	if (lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		xSemaphoreTakeRecursive(lock, portMAX_DELAY);
	}
}

static void port_unlock(void *ctx)
{
	(void)ctx;
	if (lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		xSemaphoreGiveRecursive(lock);
	}
}

// The chip looks for sync only from CARRIER_READY on (modem-sct.md 3), so a board that never sends it boots the
// baseband and then hears nothing. It goes out two ways: from the board's own carrier reading
// (d878uv2_modem_carrier, called by the radio's status path) and, whatever the board reads, rx_settle_ms after
// each return to receive.
//
// **The value is this radio's one real unknown here.** 1 says the receiver is ready, 2 the same with the
// demodulated phase reversed, which is what the family's mobile needs because its RF chip inverts the DMR audio
// output. Whether this radio's AT1846S does the same is not recorded, and with the wrong one no call decodes at
// all. Build with -DD878UV2_CR_READY=1 to try the other (docs/testing/d878uv2-bringup.md, stage 8)
#ifndef D878UV2_CR_READY
#define D878UV2_CR_READY	2
#endif
// After a transmission the receiver needs a moment before the chip should start hunting
#define CR_SETTLE_MS		14

// How the board wires the chip (modem-sct.md 4). **Taken from the family's mobile**, which runs the same baseband:
// this radio's own wiring is not recorded, so these five bytes are the second thing to question if the chip boots
// but no call decodes (docs/testing/d878uv2-bringup.md, stage 8)
static const sct_dmr_port_t port = {
	.send = port_send,
	.call_option = { 0x01, 0x40, 0x42, 0x4F, 0x08 },
	.report_field = 0xFF,
	.rx_ready = D878UV2_CR_READY,
	.rx_settle_ms = CR_SETTLE_MS,
	.ready = port_ready,
	.lock = port_lock,
	.unlock = port_unlock,
};

static void on_packet(const uint8_t *pkt, uint16_t len, void *ctx)
{
	(void)ctx;
	sct_dmr_packet(pkt, len, hal_millis());
}

static void modem_task(void *arg)
{
	static sct_rx_t rx;	// it carries its own reassembly buffer

	(void)arg;
	sct_rx_reset(&rx);
	boot_result = sct_host_boot(&host, &rx, BOOT_TIMEOUT_MS);
	booted = boot_result == SCT_BOOT_OK;
	boot_done = true;
	for (;;) {
		port_lock(NULL);
		sct_host_poll(&host, &rx, on_packet, NULL);
		sct_dmr_poll(hal_millis());
		port_unlock(NULL);
		vTaskDelay(pdMS_TO_TICKS(MODEM_POLL_MS));
	}
}

bool d878uv2_modem_init(void)
{
	sct_dmr_init(&port);
	if (crc32(__codec_slot, SCT_BOOT_BLOCK_BYTES) != BOOT_BLOCK_CRC32) {
		boot_result = SCT_BOOT_NO_BLOCK;
		boot_done = true;
		return false;
	}
	lock = xSemaphoreCreateRecursiveMutex();
	xTaskCreate(modem_task, "modem", MODEM_TASK_STACK, NULL, MODEM_TASK_PRIORITY, NULL);
	return true;
}

// board_t.modem_codec: the boot block the slot holds, the same check as the start's
const char *d878uv2_modem_codec(void)
{
	return crc32(__codec_slot, SCT_BOOT_BLOCK_BYTES) == BOOT_BLOCK_CRC32 ? "d878uv2-boot" : NULL;
}

// board_t.boot_busy: true while the chip is still booting, so the UI can wait instead of reporting no DMR
bool d878uv2_modem_booting(void)
{
	return !boot_done;
}

// The board's carrier reading, passed to the engine once the chip is up
void d878uv2_modem_carrier(bool carrier)
{
	if (booted) {
		sct_dmr_carrier(carrier ? (uint8_t)D878UV2_CR_READY : 0u);
	}
}

// One codec register, as a codec-bypass packet. The MCU has no bus to the TLV320AIC3204: it reaches it only
// through the baseband, which adds the codec's I2C address itself (sct_pkt.h). The baseband's own firmware sets
// the codec's clocks, routing and converters up; the MCU only trims gains
// (docs/spec/hardware-d878uv2.md 4.6)
bool d878uv2_codec_write(uint8_t page, uint8_t reg, uint8_t value)
{
	uint8_t pkt[SCT_CODEC_PKT_BYTES];
	sct_codec_write_t w = { .page = page, .reg = reg, .value = value };

	if (!booted) {
		return false;
	}
	sct_pkt_codec(pkt, &w);
	port_lock(NULL);
	bool ok = sct_host_send(&host, pkt, sizeof(pkt));

	port_unlock(NULL);
	return ok;
}

// The boot's result, for the diagnostics line
sct_boot_result_t d878uv2_modem_boot_result(void)
{
	return boot_result;
}

#endif	// CONFIG_DMR
