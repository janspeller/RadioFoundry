# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# AnyTone AT-D878UVII PLUS (handheld), GD32F303VG + SCT3258 DMR baseband + one AT1846S + ST7735S 128x160 on an 8080
# bus (docs/spec/hardware-d878uv2.md). It shares the platform and the baseband driver with the family's mobile and
# differs in having one receiver, a battery, a key matrix on the LCD data lines, and a Bluetooth/APRS daughter
# board on UART4 (hardware-d878uv2.md 4.5).
#
# WORK IN PROGRESS: the board's interface files are not written yet, so this board does not build. Do not commit it
# until it does: tools/ci.sh builds every board whose board.mk exists.
PLATFORM   := gd32f30x
MCU        := gd32f303
# The image starts where the vendor bootloader starts the application (hardware-d878uv2.md 1.3)
APP_BASE   := 0x08004000
# board_t .name
BOARD_T_NAME := AT-D878UVII
# Bench knobs (docs/testing/d878uv2-bringup.md)
D878UV2_DEFS ?=
MCU_DEFS   += $(D878UV2_DEFS)
BOARD_DIRS := boards/d878uv2 drivers/display/st7735 drivers/flash/spi_nand drivers/input/quadrature \
              drivers/input/keymatrix drivers/modem/sct drivers/rf/rfbus drivers/rf/subaudio drivers/rf/at1846s \
              drivers/gps/nmea_line drivers/gps/gps_rx platform/gd32f30x/gps_uart drivers/bcast/rda5807m \
              drivers/bus/i2c_gpio
# The core's storage on the NAND, with CONFIG_NAND_STORAGE (hardware-d878uv2.md 5)
BOARD_NAND_DIRS := drivers/flash/nand_sectors drivers/flash/nand_blocks
# The 128x160 panel in 16 colors: the 4 bpp frame buffer is 10240 B over 160 rows, not the RGB565 default. A screen
# has at most 16 colors (core/ui/gfx.h). The panel is addressed in portrait, MADCTL 0x88 (hardware-d878uv2.md 4.2)
MCU_DEFS   += -DGFX_INDEXED=1 -DGFX_FB_BYTES=10240u -DGFX_MAX_ROWS=160u
