# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# RT-4D feature defaults (config/features.mk lists them all). Bring-up state (docs/spec/hardware-rt4d.md, nothing run on
# a radio yet): analog FM on the BK4819, the UI on the 128x64 panel, keys, the data flash with the factory calibration,
# the battery, and the host link on the K1 cable's UART (USART6). DMR is the HF6853's own, through the call-level
# backend (CONFIG_DMR_CALL, untested on the radio). USB, the crash log, the fault trace and the stub entry follow the
# platform (platform/at32f423/config.mk).
#
# RAM is the limit: 48 KB in all, no CCM (hardware-rt4d.md 7.2, 7.2.1). The RTOS heap and the RAM knobs are in board.mk;
# the switches below that are off for RAM say so.
#
# 128x64 monochrome: no room for the large font, and no colors to set
CONFIG_FONT_XL ?= n
CONFIG_UI_COLOR ?= n
# DMR on this radio is the HF6853's own (a call-level chip, hardware-rt4d.md 5, 7.2.1): the call-level backend
# (docs/spec/dmr-call-level.md) instead of the core's burst-level stack, so nothing under CONFIG_DMR builds. The
# chip's encryption (CONFIG_DMR_AES) follows it
CONFIG_DMR ?= n
CONFIG_DMR_CALL ?= y
# The host link runs, for the debug link and the open-firmware CPS; the hotspot and PPP need DMR or more RAM
CONFIG_NET_PPP ?= n
# APRS transmit needs an AFSK path (the TX DTMF DAC on PA5), not written; receive needs FM audio at the MCU, which the
# board doesn't route (the BK4819's audio goes to the HF6853 and the amplifier only)
CONFIG_APRS ?= n
CONFIG_APRS_RX ?= n
CONFIG_APRS_IGATE ?= n
# Beeps need the tone DAC (PA4) as a PCM path (hal_audio_t), not written. The FM TX tones (1750 Hz, roger beep) and
# DTMF go through the BK4819's tone generator (the shared driver's, radio_rt4d.c): on, untested on the radio
CONFIG_BEEP ?= n
# VOX reads the microphone level through hal_power_t.mic_level, which the board doesn't have
CONFIG_VOX ?= n
CONFIG_REF_TRIM ?= n
CONFIG_AM_RX ?= n
CONFIG_BCAST_FM ?= n
CONFIG_RX_EXT ?= n
CONFIG_GPS ?= n
# Frequency detect on the BK4819's frequency scan (hardware-rt4d.md 3.1.1, the stock "Freq Detect")
CONFIG_FREQ_DETECT ?= $(CONFIG_FM)
# What the radio's hardware can't do, one switch per line with the reason (tools/boards.py checks there is one;
# docs/build-config.md, "Unsupported, not written, unconfirmed"): ci's maximal configuration leaves these off. USB
# and USB_NETWORK are in the platform's list
BOARD_UNSUPPORTED += FONT_XL	# the 128x64 panel is too small for the large font
BOARD_UNSUPPORTED += UI_COLOR	# the 128x64 panel is monochrome: no colors to set
BOARD_UNSUPPORTED += NAND_STORAGE	# the data flash is SPI NOR: 4 KB erase (hardware-rt4d.md 1)
BOARD_UNSUPPORTED += GPS	# no GPS receiver (hardware-rt4d.md 1)
BOARD_UNSUPPORTED += GPS_SKY	# needs the GPS receiver
BOARD_UNSUPPORTED += VIBRATE	# no vibration motor (hardware-rt4d.md 4)
BOARD_UNSUPPORTED += APRS_RX	# FM receive audio never reaches the MCU (hardware-rt4d.md 3: AF out goes to the HF6853 and the amplifier)
BOARD_UNSUPPORTED += APRS_IGATE	# needs APRS receive
BOARD_UNSUPPORTED += DMR	# the HF6853 runs DMR itself and gives no burst access: CONFIG_DMR_CALL instead (hardware-rt4d.md 5)
# What the hardware can do but this radio's driver or glue doesn't yet, the same way: off by default, and off in ci's
# maximal configuration too
BOARD_NOT_WRITTEN += APRS	# no AFSK transmit path yet (the TX DTMF DAC, PA5)
BOARD_NOT_WRITTEN += BEEP	# the tone DAC (PA4) isn't driven as a PCM path yet
BOARD_NOT_WRITTEN += VOX	# no hal_power_t.mic_level hook yet
BOARD_NOT_WRITTEN += REF_TRIM	# the BK4819 retune for the trim isn't written for this board
BOARD_NOT_WRITTEN += AM_RX	# the BK4819 receives AM (hardware-rt4d.md 3), the board's AM path isn't written
BOARD_NOT_WRITTEN += BCAST_FM	# the BK1080 is fitted, its driver isn't written
BOARD_NOT_WRITTEN += RX_EXT	# the wide receive ranges need the calibration bands 2-10 (hardware-rt4d.md 3.2) in the RX path
BOARD_NOT_WRITTEN += AUDIO_PROBE	# no board.audio_tap
# Supported, but the maximal configuration leaves these off too: every switch on doesn't fit the 246 KB application
# region or 48 KB RAM with call-level DMR and every translation. Maximal keeps Spanish, as the default build does
# (Italian builds on the other boards)
MAXIMAL_SKIP := LANG_IT LANG_DEFAULT_IT LANG_FR LANG_ZH LANG_DEFAULT_FR LANG_DEFAULT_ZH
# FM is the one mode every configuration needs
BOARD_REQUIRED := FM
# The stack check's source analysis unions indirect calls conservatively (radio-features.md bring-up): report, don't fail CI
STACK_CHECK_REPORT_ONLY := 1

# The boot probe's waits are long and its faults blink forever: no watchdog under it
ifneq ($(filter -DRT4D_BOOT_PROBE,$(RT4D_DEFS)),)
CONFIG_WATCHDOG := n
endif
