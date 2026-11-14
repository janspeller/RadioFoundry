# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# AT32F423 feature defaults, read after the board's config.mk (a board's ?= wins) and before config/features.mk.
# The radios on it have no USB data port: the host link runs on the programming cable's UART
CONFIG_USB ?= n
CONFIG_HOSTLINK ?= y
# The 16x32 font is for panels 240 px and wider
CONFIG_FONT_XL ?= n
# Link time optimization on our sources and the third-party code: the images are tight in 246 KB
LTO ?= 1
LTO_3P ?= 1
# What no radio on the platform has, one switch per line with the reason (docs/build-config.md, "Unsupported, not
# written, unconfirmed"): ci's maximal configuration leaves these off
BOARD_UNSUPPORTED += USB	# no USB data port: the host link runs on the programming cable's UART
BOARD_UNSUPPORTED += USB_NETWORK	# no USB data port for the network adapter
# What the platform's code doesn't do yet. The crash log and the fault trace need the fault capture and a kept RAM
# record (platform/stm32f4/crash_stm32.c is the model, to be shared rather than copied); the stub entry needs a RAM
# flash stub for this chip
CONFIG_CRASH_LOG ?= n
CONFIG_FAULT_TRACE ?= n
CONFIG_STUB_ENTRY ?= n
BOARD_NOT_WRITTEN += CRASH_LOG	# the platform's fault capture and kept record aren't written (above)
BOARD_NOT_WRITTEN += CRASH_SCREEN	# needs the crash log
BOARD_NOT_WRITTEN += CRASH_REPORT	# needs the crash log
BOARD_NOT_WRITTEN += FAULT_TRACE	# needs the platform's fault capture (above)
BOARD_NOT_WRITTEN += STUB_ENTRY	# no RAM flash stub for this chip yet
