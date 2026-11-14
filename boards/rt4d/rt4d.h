// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Internal interfaces between the RT-4D board files
#ifndef RT4D_H
#define RT4D_H

#include <stdint.h>
#include <stdbool.h>
#include "board.h"
#include "platform.h"
#include "cal_rt4d.h"
#include "spi_flash/spi_flash.h"

// The data flash is the platform's SPI NOR glue (platform/at32f423/spi_flash.c, configured in board.c)
#define rt4d_flash at32_spi_flash

extern const hal_display_t rt4d_display;
extern const hal_input_t rt4d_input;
extern const hal_radio_t rt4d_radio;
extern const hal_dmr_call_t rt4d_dmr_call;	// CONFIG_DMR_CALL (dmr_rt4d.c)
extern const hal_power_t rt4d_power;

// Physical key bits returned by rt4d_input.scan() (docs/spec/hardware-rt4d.md 4). F and EXT are the keys printed so on
// the matrix sheet; SK1 is the side key on KEYIN2, SK2 the one on KEYIN3
enum {
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
	KEY_STAR, KEY_HASH, KEY_UP, KEY_DOWN, KEY_F, KEY_EXT, KEY_AB,
	KEY_SK1, KEY_SK2, KEY_PTT,
	KEY_COUNT
};

// Key scans with the PTT pin low that were ignored as host serial data (the PTT shares the cable's RX line)
uint32_t rt4d_input_ptt_ignored(void);
bool rt4d_radio_transmitting(void);	// the radio is keyed (the shared PTT line's gate)
// DMR: the HF6853's decoded audio to the speaker or not (hf6853_io_t.rx_audio, radio_rt4d.c)
void rt4d_dmr_audio(bool on);
// The debug link's CHIP_REG for the BK4819 (board.chip_reg)
bool rt4d_chip_reg(uint8_t chip, uint8_t page, uint8_t reg, uint32_t *val, bool write);
// The board's debug link view keys (board.view_line), the DMR chip driver's after them (dmr_rt4d.c)
bool rt4d_view_line(uint8_t i, char *buf, uint8_t len);
bool rt4d_dmr_view_line(uint8_t i, char *buf, uint8_t len);

#endif
