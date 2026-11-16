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
#if CONFIG_DMR || CONFIG_FM	// both modes use the baseband's codec path
#include "pins.h"
#include "mcu_io.h"
#include "hal_time.h"
#if CONFIG_DMR
#include "modem/sct/sct_dmr.h"
#endif
#if CONFIG_FM
#include "modem/sct/sct_analog.h"
static sct_analog_t analog;
#endif
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
// pair the family's mobile carries (hardware-d878uv2.md 1.1). The gains below are bring-up defaults; the per-radio
// audio calibration values and the codec routing still need confirmation

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

#include "audio_setup.h"

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
#if CONFIG_DIAG
static uint8_t codec_snapshot[2][128];
static bool codec_snapshot_valid[2];

static void snapshot_codec(sct_rx_t *rx)
{
	// TI's 0x18 seven-bit address is 0x30 in the SCT raw-I2C command.
	for (uint8_t page = 0; page < 2; page++) {
		codec_snapshot_valid[page] = sct_host_i2c_page(&host, rx, 0x30, page, 200) &&
			sct_host_i2c_read(&host, rx, 0x30, 0, codec_snapshot[page], 128, 200) &&
			codec_snapshot[page][0] == page;
	}
	(void)sct_host_i2c_page(&host, rx, 0x30, 0, 200);
}

int d878uv2_codec_snapshot(uint8_t page, uint8_t reg)
{
	return page < 2 && reg < 128 && codec_snapshot_valid[page] ? codec_snapshot[page][reg] : -1;
}
#endif
static sct_boot_result_t boot_result = SCT_BOOT_SILENT;

static bool port_send(const uint8_t *buf, uint16_t len, void *ctx)
{
	(void)ctx;
	return sct_host_send(&host, buf, len);
}

static volatile int8_t mic_offset_db;

void d878uv2_modem_mic_gain(int8_t db)
{
	if (db < -10) db = -10;
	if (db > 10) db = 10;
	mic_offset_db = db;
}

int8_t d878uv2_modem_mic_offset(void) { return mic_offset_db; }
uint8_t d878uv2_modem_mic_pga(void) { return codec_mic_pga(mic_offset_db); }

#if CONFIG_DMR

static bool port_ready(void *ctx)
{
	(void)ctx;
#if CONFIG_FM
	return booted && analog.digital_ready && !analog.enabled;
#else
	return booted;
#endif
}
#endif

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

#if CONFIG_DMR
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

// The modulator's gain, sent at each start and before every transmission. The chip's own default leaves both
// kinds of transmission weak on this radio: FM deviation the other radio can barely hear, and DMR it cannot
// decode at all. Q16.11, so 2048 is 0 dB and 4096 is +6 dB.
//
// **A guess in the right units.** The radio's own deviation lives in its calibration (the band record's MOD
// byte, docs/spec/hardware-d878uv2.md 5), and how that reaches the modulator is not established, so this is a
// build knob to find the level by experiment: -DD878UV2_MOD_GAIN=<q16.11>
#ifndef D878UV2_MOD_GAIN
#define D878UV2_MOD_GAIN	2048u
#endif
static const uint8_t mod_gain[] = { (uint8_t)(D878UV2_MOD_GAIN >> 8), (uint8_t)(D878UV2_MOD_GAIN & 0xFFu) };
static const sct_field_t tx_gain_fields[] = {
	{ SCT_F_MOD_GAIN, sizeof(mod_gain), mod_gain },
};

static uint8_t port_gain_fields(const sct_field_t **fields, void *ctx)
{
	(void)ctx;
	codec_mic_offset(mic_offset_db);
	*fields = tx_gain_fields;
	return (uint8_t)(sizeof(tx_gain_fields) / sizeof(tx_gain_fields[0]));
}

// How the board wires the chip (modem-sct.md 4). **Taken from the family's mobile**, which runs the same baseband:
// this radio's own wiring is not recorded, so these five bytes are the second thing to question if the chip boots
// but no call decodes (docs/testing/d878uv2-bringup.md, stage 8)
static const sct_dmr_port_t port = {
	.send = port_send,
	.call_option = { 0x01, 0x40, 0x42, 0x4F, 0x08 },
	.codec_start = codec_writes,
	.codec_start_count = CODEC_MIC_COUNT,
	.codec_tx = &codec_writes[1],
	.tx_fields = port_gain_fields,
	.start_fields = tx_gain_fields,
	.start_count = sizeof(tx_gain_fields) / sizeof(tx_gain_fields[0]),
	.report_field = 0xFF,
	.burst_edges = d878uv2_slot_edges,
	.rx_ready = D878UV2_CR_READY,
	.rx_settle_ms = CR_SETTLE_MS,
	.ready = port_ready,
	.lock = port_lock,
	.unlock = port_unlock,
};

#endif

static void on_packet(const uint8_t *pkt, uint16_t len, void *ctx)
{
	(void)ctx;
#if CONFIG_FM
	if (sct_analog_owns_link(&analog)) {
		sct_analog_packet(&analog, pkt, len);
		return;
	}
#endif
#if CONFIG_DMR
	sct_dmr_packet(pkt, len, hal_millis());
#endif
}

static void modem_task(void *arg)
{
	static sct_rx_t rx;	// it carries its own reassembly buffer

	(void)arg;
	sct_rx_reset(&rx);
	boot_result = sct_host_boot(&host, &rx, BOOT_TIMEOUT_MS);
#if CONFIG_DIAG
	if (boot_result == SCT_BOOT_OK) {
		snapshot_codec(&rx);
	}
#endif
	booted = boot_result == SCT_BOOT_OK;
	boot_done = true;
	for (;;) {
		port_lock(NULL);
		sct_host_poll(&host, &rx, on_packet, NULL);
#if CONFIG_FM
		if (booted && sct_analog_owns_link(&analog)) {
			sct_analog_poll(&analog, hal_millis());
		} else
#endif
		{
#if CONFIG_DMR
			sct_dmr_poll(hal_millis());
#endif
		}
		port_unlock(NULL);
		vTaskDelay(pdMS_TO_TICKS(MODEM_POLL_MS));
	}
}

bool d878uv2_modem_init(void)
{
#if CONFIG_DMR
	sct_dmr_init(&port);
#endif
#if CONFIG_FM
	sct_analog_init(&analog, port_send, NULL);
	analog.tx_codec = codec_writes;
	analog.tx_codec_count = CODEC_MIC_COUNT;
#endif
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
	if (!boot_done) return true;
#if CONFIG_FM
	if (booted && analog.error == 0 &&
	    ((analog.enabled && !analog.ready) || (!analog.enabled && !analog.digital_ready))) return true;
#endif
	return false;
}

#if CONFIG_DMR
// The board's carrier reading, passed to the engine once the chip is up
void d878uv2_modem_carrier(bool carrier)
{
	if (booted) {
		sct_dmr_carrier(carrier ? (uint8_t)D878UV2_CR_READY : 0u);
	}
}

#endif

#if CONFIG_FM
void d878uv2_modem_analog(bool enabled, bool tx, bool narrow)
{
	port_lock(NULL);
	if (enabled && tx) codec_mic_offset(mic_offset_db);
	sct_analog_request(&analog, enabled, tx, narrow);
	port_unlock(NULL);
}

#if CONFIG_DIAG
bool d878uv2_modem_loopback(bool enabled)
{
	port_lock(NULL);
	bool ok = booted && sct_analog_loopback(&analog, enabled);
	port_unlock(NULL);
	return ok;
}
#endif

uint32_t d878uv2_analog_value(uint8_t key)
{
	port_lock(NULL);
	if (key == 11) {
		uint8_t count = analog.tx_codec_index;
		port_unlock(NULL);
		return count;
	}
#if CONFIG_DIAG
	if (key == 10) {
		bool value = analog.loopback;
		port_unlock(NULL);
		return value;
	}
#endif
	uint32_t value = key == 0 ? analog.enabled : key == 1 ? analog.ready : key == 2 ? analog.want_tx :
		key == 3 ? analog.pending : key == 4 ? analog.error : key == 5 ? analog.commands :
		key == 6 ? analog.timeouts : key == 7 ? analog.last_id : key == 8 ? analog.digital_ready : analog.analog_loaded;
	port_unlock(NULL);
	return value;
}
#endif

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

#endif	// CONFIG_DMR || CONFIG_FM
