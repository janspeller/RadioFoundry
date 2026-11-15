# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# AnyTone AT-D878UVII PLUS feature defaults (config/features.mk lists them all). Bring-up state:
# docs/testing/d878uv2-bringup.md. RAM is the limit: 96 KB, no CCM. Flash is not: a measured build of this
# platform's other SCT3258 board uses 38 % of a region this radio has 8 KB more of (hardware-d878uv2.md 1.3)
#
# DMR runs through the SCT3258 baseband, which carries its own vocoder and plays the voice to the codec itself
# (CAP_MODEM_VOICE, docs/spec/modem-sct.md 7). Its driver, drivers/modem/sct, is shared with every SCT3258 radio
CONFIG_DMR ?= y
# What needs the voice frames on the MCU is not written (below); with DMR on, what needs RAM this radio doesn't
# have is off, as on the family's other board: the USB network adapter and the IP gateway behind it, and zone sharing
CONFIG_HOTSPOT ?= n
CONFIG_SFR ?= n
CONFIG_DMR_PRIVACY ?= n
ifeq ($(CONFIG_DMR),y)
CONFIG_USB_NETWORK ?= n
CONFIG_ZONE_SHARE ?= n
endif
# 128x160 like the other small color radios
CONFIG_FONT_XL ?= n
# The core's storage in RadioFoundry's own NAND block range
CONFIG_NAND_STORAGE ?= y
# A handheld on a battery, unlike the family's mobile: the battery monitor and its warnings are on (ADC1 channel 11,
# hardware-d878uv2.md 4.3)
CONFIG_BATTERY_SAVE ?= y
# PTT and keys from the Bluetooth/APRS daughter board on UART4. Its protocol is not recorded, so the driver is not
# written yet (below)
CONFIG_BT_PTT ?= n
# APRS receive samples the receive audio and does not fit with DMR on, as on the family's other board. This radio carries
# a CMX865 FSK modem on the daughter board that may serve instead; not written (hardware-d878uv2.md 4.4)
CONFIG_APRS_RX ?= n
# USRP needs PCM both ways, which the baseband never gives the MCU
CONFIG_NET_USRP ?= n
# One receiver can alternate channels in software; no second RF chip is needed for dual watch.
CONFIG_AM_RX ?= n
# What the radio's hardware can't do, one switch per line with the reason (tools/boards.py checks there is one;
# docs/build-config.md, "Unsupported, not written, unconfirmed")
BOARD_UNSUPPORTED += FONT_XL	# the 128x160 panel is too small for the large font (it is for 240 px and wider)
BOARD_UNSUPPORTED += AM_RX	# no air band receiver on the I2C bus (hardware-d878uv2.md 4.1)
# Left out of tools/ci.sh's maximal configuration: what doesn't fit the RAM with DMR on
MAXIMAL_SKIP := USB_NETWORK NET_PPP DMR_IP_GATEWAY NET_TCP NET_KISS NET_HTTP NET_HTTP_GZIP NET_WEBCTL NET_RIGCTL \
	NET_APRSIS MDNS TFTP TFTP_IDDB SNMP DMR_CONTROL_STATION ZONE_SHARE APRS_RX APRS_IGATE
# What the hardware can do but this radio's driver or glue doesn't yet
BOARD_NOT_WRITTEN += HOTSPOT	# needs the voice frames on the MCU: the baseband's frame bypass isn't written
BOARD_NOT_WRITTEN += HOTSPOT_SPLIT	# needs the hotspot (above)
BOARD_NOT_WRITTEN += SFR	# needs the voice frames on the MCU (above)
BOARD_NOT_WRITTEN += SFR_DUALPATH_RX	# needs SFR (above)
BOARD_NOT_WRITTEN += SFR_CWID	# needs SFR (above)
BOARD_NOT_WRITTEN += DMR_PRIVACY	# the cipher runs on the 49-bit voice frames, which the baseband can route
				# through the MCU (modem-sct.md 7); that bypass isn't written
BOARD_NOT_WRITTEN += AUDIO_PROBE	# the probe taps an audio chip's PCM path; the voice stays in the baseband here
BOARD_NOT_WRITTEN += NET_USRP	# needs an audio tap (PCM both ways): only receive audio reaches the MCU
BOARD_NOT_WRITTEN += BT_PTT	# the daughter board's protocol over UART4 is not recorded (hardware-d878uv2.md 4.4)
BOARD_NOT_WRITTEN += VIBRATE	# whether the radio has a motor is not recorded
BOARD_REQUIRED := FM
