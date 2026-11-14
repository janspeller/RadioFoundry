/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
/* AnyTone AT-D878UVII PLUS, GD32F303VG (1 MB flash, 96 KB SRAM), application behind the vendor bootloader
 * (docs/spec/hardware-d878uv2.md 1.3). The bootloader holds 0x08000000..0x08003FFF and starts the image at
 * 0x08004000 (board.mk APP_BASE), 8 KB lower than the AT-D578UV's. RadioFoundry's image stays below the modem boot
 * block slot (0x080FF000..0x08100000), where the boot block the DMR baseband needs is merged from the user's stock
 * file on their machine (hardware-anytone.md 2.2). On this 1 MB part the slot is its last 4 KB.
 * The layout is the platform's (platform/gd32f30x/common.ld). */

/* The registry and the flashing tools know the slot as the codec slot (tools/boards.py): like a codec, the block is
 * the radio maker's and merged on the user's machine (tools/modem_boot.py) */
__codec_slot = 0x080FF000;
__codec_slot_end = 0x08100000;

MEMORY
{
	FLASH  (rx)  : ORIGIN = __app_start, LENGTH = __codec_slot - __app_start
	RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 96K - 272
	/* The crash record and the restart cause's words, kept across a reset (platform/gd32f30x/common.ld) */
	KEPT   (rw)  : ORIGIN = 0x20000000 + 96K - 272, LENGTH = 272
}

INCLUDE common.ld
