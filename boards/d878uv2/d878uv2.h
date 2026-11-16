// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT-D878UVII PLUS board internals shared between its files (docs/spec/hardware-d878uv2.md)
#ifndef D878UV2_H
#define D878UV2_H

#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "config.h"

extern const hal_display_t d878uv2_display;
extern const hal_input_t d878uv2_input;
extern const hal_power_t d878uv2_power;
extern const hal_radio_t d878uv2_radio;

// board_rf_safe's part: the PA gate down and the transmit level at zero. Plain register writes only
void d878uv2_rf_safe(void);
// The RF chip's GPIO shadow (register 0x1F) as last written: the band and front-end switching
uint16_t d878uv2_rf_gpio(void);
// The baseband's slot-timing line's edges, rising and falling: the DMR engine takes differences within a
// transmission to know when its last burst has left the air (docs/spec/modem-sct.md 3)
uint32_t d878uv2_slot_edges(void);
#if CONFIG_GPS
// The GPS module's supply, on the RF chip's gpio2 (radio_d878uv2.c); its UART is gps_d878uv2.c
void d878uv2_gps_power(bool on);
void d878uv2_gps_init(void);
#endif
#if CONFIG_DIAG
bool d878uv2_diag_line(uint8_t i, char *buf, uint8_t len);
bool d878uv2_view_line(uint8_t i, char *buf, uint8_t len);
int d878uv2_cal_bytes(uint8_t chip, uint8_t page, uint8_t reg, uint8_t *out, uint8_t cap);
int d878uv2_chip_bytes(uint8_t chip, uint8_t page, uint8_t reg, uint8_t *out, uint8_t cap);
uint16_t d878uv2_tuning_value(uint8_t key);
#if CONFIG_DMR || CONFIG_FM
int d878uv2_codec_snapshot(uint8_t page, uint8_t reg);
bool d878uv2_audio_reg(uint8_t chip, uint8_t page, uint8_t reg, uint32_t *value, bool write);
#endif
void d878uv2_diag_reset(void);
#endif

// The keys by their printed names (docs/spec/hardware-d878uv2.md 4.1.1). The front panel is a 3 x 4 numeric
// keypad with MENU, EXIT and the two arrows above it; P1 and P2 sit beside MENU and the arrows on matrix column 1;
// PF1 and PF2 are the side keys below PTT and PF3 is the key on top. PTT, PF1 and PF2 are direct inputs, and PF3
// is a direct input on the same pin as matrix column 1.
enum {
	KEY_MENU, KEY_EXIT, KEY_UP, KEY_DOWN,
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
	KEY_STAR, KEY_HASH,
	KEY_P1, KEY_P2,
	KEY_PF1, KEY_PF2, KEY_PF3,
	KEY_PTT,
	KEY_COUNT
};
_Static_assert(KEY_COUNT <= 32, "the key scan is a 32-bit mask");

// The key matrix scan, for the diagnostics line: the raw sense reading per drive line (hardware-d878uv2.md 4.1.1)
void d878uv2_key_raw(uint8_t raw[8]);
// The direct inputs as pressed bits: 0 PTT, 1 PF3, 2 PF1, 3 PF2, 4 the accessory detect on PA13
uint8_t d878uv2_key_direct(void);
uint8_t d878uv2_key_idle(void);

// --- SPI NAND (nand_d878uv2.c, docs/spec/hardware-d878uv2.md 4.1) ---
// RadioFoundry's own blocks, above everything the stock firmware writes (it stops at block 1741). The same layout
// the family's other SCT3258 board uses on the same 2 Gbit part
#define D878UV2_NAND_IDDB_FIRST		1792u
#define D878UV2_NAND_IDDB_BLOCKS	224u
#define D878UV2_NAND_IDDB_LOGICAL	192u	// logical blocks: 24 MB
#define D878UV2_NAND_RFOUNDRY_FIRST	2016u
#define D878UV2_NAND_RFOUNDRY_BLOCKS	32u

bool d878uv2_nand_init(void);
bool d878uv2_nand_ready(void);
uint16_t d878uv2_nand_id(void);
bool d878uv2_nand_read(uint32_t addr, void *buf, uint32_t len);
bool d878uv2_nand_read_any(uint32_t addr, void *buf, uint32_t len, uint32_t *ecc_failed);
// The factory's regions (calibration), read with the chip's on-die ECC off: see the note in nand_d878uv2.c
bool d878uv2_nand_read_factory(uint32_t addr, void *buf, uint32_t len);
void d878uv2_nand_lock(void);
void d878uv2_nand_unlock(void);
#if CONFIG_NAND_STORAGE
#include "flash/nand_sectors/nand_sectors.h"
#include "flash/nand_blocks/nand_blocks.h"
extern const nand_dev_t d878uv2_nand_dev;
extern const nand_dev_t d878uv2_nand_iddb_dev;

// The core's data flash over those two ranges (flash_d878uv2.c)
extern hal_flash_t d878uv2_flash;
bool d878uv2_flash_init(void);
void d878uv2_flash_stats(nand_sectors_stats_t *st);
void d878uv2_flash_ext_stats(nand_blocks_stats_t *st);
#endif

#if CONFIG_DMR || CONFIG_FM
// --- The audio/DMR baseband (modem_d878uv2.c, docs/spec/modem-sct.md) ---
#include "modem/sct/sct_host.h"
bool d878uv2_modem_init(void);
void d878uv2_modem_mic_gain(int8_t db);
int8_t d878uv2_modem_mic_offset(void);
uint8_t d878uv2_modem_mic_pga(void);
const char *d878uv2_modem_codec(void);
bool d878uv2_modem_booting(void);
void d878uv2_modem_carrier(bool carrier);
sct_boot_result_t d878uv2_modem_boot_result(void);
// The modulator's gain for the frequency about to be keyed, from this radio's calibration
void d878uv2_modem_set_deviation(uint32_t hz);
// One TLV320AIC3204 register through the baseband (hardware-d878uv2.md 4.6)
bool d878uv2_codec_write(uint8_t page, uint8_t reg, uint8_t value);
#if CONFIG_FM
void d878uv2_modem_analog(bool enabled, bool tx, bool narrow);
#if CONFIG_DIAG
bool d878uv2_modem_loopback(bool enabled);
#endif
uint32_t d878uv2_analog_value(uint8_t key);
#endif
#endif

// --- Factory calibration, read from the NAND (cal_d878uv2.c, docs/spec/hardware-d878uv2.md 5) ---
bool d878uv2_cal_load(void);
bool d878uv2_cal_ready(void);
bool d878uv2_cal_control(uint16_t *value);
int d878uv2_cal_map_count(void);
uint32_t d878uv2_cal_map_info(uint8_t key);
bool d878uv2_cal_tx_drive(uint8_t level, uint32_t hz, uint8_t *drive);
// The modulator's gain for a frequency: the radio's own deviation, or the factory default
uint16_t d878uv2_cal_mod_gain(uint32_t hz);

// The radio's own audio values (docs/spec/hardware-d878uv2.md 4.6.6): three gain bytes the baseband passes to the
// codec, and three 16-bit baseband levels. from_radio is false when the factory defaults were used instead
typedef struct {
	uint8_t lol, lor, mic_r;
	uint8_t misc[5];	// calibration +0x4A..+0x4E for MISC_GAIN
	uint16_t demod, spk_gain, mic_gain;
	bool from_radio;
} d878uv2_cal_audio_t;
void d878uv2_cal_audio(d878uv2_cal_audio_t *a);
// The PA6 tuning voltage's duty for a frequency (hardware-d878uv2.md 5.1.2)
bool d878uv2_cal_tune_duty(uint32_t hz, uint16_t *duty);

// LCD backlight, 0..100 %: TIM8 channel 2's complementary output on PB0 (power_d878uv2.c, hardware-d878uv2.md 4.3)
void d878uv2_backlight(uint8_t percent);

#endif
