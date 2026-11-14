// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The system descriptor (hal_system.h): the chip's identity (RM 1.3: flash size and the 96-bit unique ID; RM 25.4: the
// debug IDCODE) and the reboot. A reset always goes through the vendor bootloader, which starts the application again
// unless PTT is held (hardware-rt4d.md 6). No ROM bootloader entry: BOOT0 is a pin (hardware-rt4d.md 6.2)
#include <string.h>
#include "platform.h"
#include "board.h"

static bool chip(hal_chip_t *c)
{
	memset(c, 0, sizeof *c);
	c->idcode = DEBUGMCU->IDCODE;
	c->flash_kb = *(const volatile uint16_t *)AT32_F_SIZE_ADDR;
	const volatile uint32_t *uid = (const volatile uint32_t *)AT32_UID_ADDR;
	for (int i = 0; i < 3; i++) {
		c->uid[i] = uid[i];
	}
	c->family = HAL_CHIP_AT32F4;
	return true;
}

void at32_reboot(void)
{
	__disable_irq();
	board_rf_safe();
	NVIC_SystemReset();
}

static bool reboot(uint8_t mode)
{
	if (mode != HAL_REBOOT_NORMAL) {
		return false;
	}
	at32_reboot();
}

const hal_system_t at32_system = {
	.chip = chip,
	.reboot = reboot,
};
