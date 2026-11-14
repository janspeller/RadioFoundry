/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
/* Radtel RT-4D, AT32F423CCT7: 256 KB flash, 48 KB SRAM (docs/spec/hardware-rt4d.md 1). The vendor bootloader holds
 * 0x08000000-0x080027FF and starts the application at 0x08002800 (__app_start, board.mk APP_BASE); the application region
 * runs to the end of flash, 251,904 bytes, which the bootloader writes whole (6.1). The flashing tools read the region
 * from this script (tools/boards.py). */

MEMORY
{
	FLASH   (rx)  : ORIGIN = __app_start, LENGTH = 0x08040000 - __app_start
	RAM     (xrw) : ORIGIN = 0x20000000, LENGTH = 48K
}

INCLUDE common.ld
