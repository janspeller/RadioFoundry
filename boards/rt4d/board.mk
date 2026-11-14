# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Radtel RT-4D (iRadio DM-4R): AT32F423CCT7 + BK4819 + HF6853 DMR chip + 128x64 monochrome ST7565-class LCD on a
# bit-banged serial bus + 16 MB SPI NOR (docs/spec/hardware-rt4d.md). The PC link is USART6 on the K1 cable
PLATFORM   := at32f423
# Behind the 10 KB vendor bootloader, which starts the application here (hardware-rt4d.md 1, 6.1)
APP_BASE   := 0x08002800
BOARD_DIRS := boards/rt4d drivers/rf/rfbus drivers/rf/bk4819 drivers/rf/subaudio drivers/display/st7567 \
	drivers/flash/spi_nor drivers/input/keymatrix drivers/input/sharedptt \
	platform/at32f423/spi_flash
# The call-level DMR chip's driver (CONFIG_DMR_CALL, docs/spec/dmr-call-level.md 7)
BOARD_DMR_CALL_DIRS := drivers/dmr/hf6853
# RadioFoundry's data sits from 7 MB up in the 16 MB data flash, far above everything the stock application uses
# (hardware-rt4d.md 3.3: up to the font at 0x14C000), as on the other 16 MB radios (board.c flash map)
MCU_DEFS   := -DCODEPLUG_OPENFW_EEPROM_BASE=0x700000u -DCODEPLUG_OPENFW_FLASH_BASE=0x720000u
# The 128x64 panel is 1 bit per pixel (display_rt4d.c): 1 KB of frame buffer and 64 rows, not the 40 KB / 160 row default
MCU_DEFS   += -DGFX_FB_BYTES=1024u -DGFX_MAX_ROWS=64u
# 48 KB of RAM in all (hardware-rt4d.md 7.2, 7.2.1): the debug link's log ring at a quarter of its default, fewer
# cached talk group names, the 1.5 KB CPS request buffer (open-firmware-cps.md 1.1), and task stacks cut to what
# make stack-check reads plus its margin. The heap is those stacks (10,272 B), the kernel's task and queue blocks
# (about 2 KB) and some 2 KB to spare: tasks.heap_min_free on the radio says whether it holds
MCU_DEFS   += -DDLOG_RING_BYTES=512 -DRADIO_NAMES_MAX=9u -DCPS_REQUEST_MAX=1536
MCU_DEFS   += -DHOSTLINK_TASK_STACK=496 -DSTART_TASK_STACK=512 -DconfigTIMER_TASK_STACK_DEPTH=160 \
	-DconfigTOTAL_HEAP_SIZE=14336
# Bench knobs: RT4D_DEFS="-DRT4D_KEEP_SWD" leaves PA13 / PA14 to the debugger instead of the LEDs
# RT4D_DEFS="-DRT4D_BOOT_PROBE": the boot-stage probe, progress blinked on the backlight (PA3) with the watchdog off
# (docs/testing/rt4d-bringup.md)
RT4D_DEFS  ?=
MCU_DEFS   += $(RT4D_DEFS)
ifneq ($(filter -DRT4D_BOOT_PROBE,$(RT4D_DEFS)),)
MCU_DEFS   += -DAT32_BOOT_PROBE -DAT32_PROBE_PORT=0 -DAT32_PROBE_PIN=3
endif
