<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AT-D878UVII PLUS bring-up checklist

No board for the handhelds yet. The family's platform, baseband driver and flashing route are written and running on
the AT-D578UV, so this radio is a board on top of them, not a new port: `platform/gd32f30x`, `drivers/modem/sct`
([modem-sct.md](../spec/modem-sct.md)), `drivers/flash/spi_nand`, `drivers/rf/at1846s`, `drivers/display/st7735` and
`rfoundry`'s modem boot slot all stand. Copy `boards/d578uv` as the model.

The chips are confirmed: the board in the public internal photos of FCC ID T4KD878UVII carries a `GD32F303VGT6`,
an `SCT3258TD`, a `TC58CVG1S3H` NAND, an `AIC3204` codec, an `AT1846S` and an `AT6558` GNSS chip
([hardware-anytone.md](../spec/hardware-anytone.md) 1). The same filing's schematic and parts list are withheld, so
the pin map was read from the stock firmware instead, and is recorded in [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4. **Nothing in it is confirmed on a
radio**, so the bench work is confirmation, not discovery.

**This page is the delta.** The stages, the diagnostics lines and what to change when one fails are in
[d578uv-bringup.md](d578uv-bringup.md); only what this radio does differently is below. Hardware facts:
[hardware-anytone.md](../spec/hardware-anytone.md), whose section numbers are in parentheses, and
[modem-sct.md](../spec/modem-sct.md) for the baseband.

The board id below (`d878uv2`) is what this page assumes; the registry settles it when the board lands. The 868 and
878 are close enough that they will probably share a board directory the way `d578uv` and `d578uva` do, with the
868's smaller flash and RAM as the constraint.

## What this radio changes

| Item | AT-D578UV (done) | This radio |
| --- | --- | --- |
| Form | mobile, two receivers (4x QA4818N), no battery | handheld, one AT1846S, a battery to measure |
| `APP_BASE` | 0x08006000 | **0x08004000**: the 4.00 image's reset vector is 0x08004C91 (3). The vendor bootloader below it is never written |
| MCU | GD32F303VKT6 (3 MB flash), or AT32F435/437 on late units | **GD32F303VGT6**, marked on the board: 1 MB flash, 96 KB RAM. The same RAM, and no less usable flash: the 578 caps its image at 1 MB anyway (below) |
| Update mode | MENU + EXIT at power-on | **PTT + PF3** at power-on, the red LED blinking (3) |
| Baseband | SCT3258 on a bit-banged parallel port | the same chip and driver. The 868's host port is data PD8-PD15, HCSN PD2, HWRN PD3, HRDN PD4, HOBIB PD5, RSTN PD6, the PIO1 strap PC12, enables PA8 and PC6 (4.1) |
| Boot block | from the stock `.CDD` into the modem boot slot | unchanged: the same 2048 bytes, confirmed in the 878UVII PLUS 4.00 image (2.2), found by the same prefix search |
| Display | 160x128, MADCTL 0xC8 | a larger screen, but **the same controller and framebuffer**: ST7735S-class, 128x160, MADCTL 0x88, the 868's init value for value, on the same pins. One `st7735` 8080 transport serves both radios. The 868's RD strobe on PE15 does not exist here ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.2) |
| Keypad | two analog ladders plus a serial keypad microphone | a key matrix on the LCD data lines PE7-PE14 with **three** sense lines (PB1, PB2, PC5), plus four direct inputs wired to ground: **PTT on PA10**, PA14 and PE1 active low, and PA13 on an internal pull-down, active high ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.1, 4.3) |
| Bluetooth and APRS | the BT01 module on `drivers/input/bt_at` | a daughter board (`APRS-BT 201208 V2.1`) with its own `HC32F005` Cortex-M0+, a CML **`CMX865` Bell-202 FSK modem** and a Cypress `CYW20707` module. **UART4, TX PC10, RX PC11, 115200**, reset on PA1, status out on PE15 ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.4). Its own protocol is not recorded |
| NAND | 2 Gbit `TC58CVG1S3H` | the same part, so `nand_d578uv.c` carries over with this radio's pins. The vendor firmware will not boot on 1 Gbit (1) |
| Power | a 11:1 supply divider, no battery | a battery: `battery_mv`, the warning levels, no `CAP_NO_BATTERY`. The 868 reads ADC channels 10-12 on PC0-PC2 (4.3) |

**What is left to find.** The pin map is recorded ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4), but none of it has been seen on a radio, and section 6
of that page lists eight things the image did not settle. The bench confirms the map; it no longer has to discover it.

## The memory map

Settled by the MCU and the family's convention; nothing here waits for the bench.

| Item | Value | Why |
| --- | --- | --- |
| `APP_BASE` | 0x08004000 | The vendor bootloader holds 0x08000000-0x08003FFF and starts the image there; the 4.00 image's reset vector is 0x08004C91 (3). The 578's is 0x08006000, so this is not a copy |
| `__codec_slot` | 0x080FF000-0x08100000 | The modem boot slot, at the same address as on both 578 builds. On a 1 MB part that is its last 4 KB |
| Application region | 0x08004000-0x080FF000, 1004 KB | **8 KB more than the 578 board has**, which caps at 1 MB so its GD32 and AT32 builds share the slot address. Flash is not the constraint here; RAM is, at 96 KB with no CCM, exactly as on the 578 |
| RAM | 96 KB less 272 bytes, with the kept crash record on top | the platform's layout (`platform/gd32f30x/common.ld`) |

So the 578's `config.mk` is a reasonable starting point: its feature trimming is for 96 KB of RAM, which this radio
shares, not for flash. Measured on the AT-D578UV's default build: **flash 387,348 of 1,019,904 bytes (38 %), RAM
92,392 of 98,032 (94 %)**, with the kept crash record full at 272 bytes. Flash has room to spare on either radio;
RAM is what a new feature has to fit.

## Confirming the pin map

The map in [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4 was read from this radio's own stock firmware, not from the 868's, and the
868's map held pin for pin everywhere except the four places that page's 4.5 lists. It is still unconfirmed on a
radio, so check it before trusting a bus.

**Method: continuity, board unpowered.** A meter from each chip's known pin back to the MCU pin. The chips are
confirmed parts with public pinouts (1.1), so each is a check.

Start with the NAND: its package is the standard 8-pin serial-flash pinout, so it is four probes, and it settles
whether the firmware's map matches the board in front of you.

| Chip pin | Function | Expected |
| --- | --- | --- |
| 1 | CS# | PB8 |
| 6 | SCK | PB14 |
| 5 | SI / IO0 | PB15 |
| 2 | SO / IO1 | PA15 |

Then the buses in the order they unblock the stages: the baseband host port (PD8-PD15 with PD2-PD6, stage 6), the
LCD and the keypad that shares its data lines (stage 2 and 3), the AT1846S three-wire bus (stages 4 and 7), the TX
power DAC on PA4 with the PA gate on PC4 and PE6 keyed from PD0 (stages 7 to 9), and UART4 to the daughter board.

The four to watch, because they are this radio's and not the 868's: **no LCD read strobe**, PE15 used by the
Bluetooth board instead, the backlight on PB0 (TIM8 channel 2 complementary) and the tuning voltage on PA6
(TIM3 channel 1), and I2C as SDA PB6 / SCL PB7, which contradicts what the 868 page records.

Mark every pin confirmed on a radio as such in [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.

## 0. Before anything

The 578's stage 0 applies, with these differences. Nothing here writes to the radio.

1. **Keep the stock `.CDD`** for the modem boot slot, as the 578 does (`codecs/`). The block is already confirmed
   present in the 4.00 image, so `rfoundry` needs nothing new; without it the radio is FM only (`bb no block`).
2. **Back up the codeplug** with the stock CPS first. The "MCU reboot" a vendor update ends with (PTT + PF1 at
   power-on) erases it (3).
3. **Prove the recovery path answers while the radio is still stock**: PTT + PF3 at power-on, then `rfoundry detect`.
   That is the way back from every step below, and `[flash.bootloader]` in the board's `board.toml` has to match what
   it reports.
4. **The MCU is settled**: `GD32F303VGT6`, read off the board in the FCC filing's internal photos, so there is
   nothing to read off the chip.
5. **Check SWD under reset.** The pads are marked "AF" beside the MCU; the vendor application disables SWJ early, so
   attach with NRST (MCU pin 14) held low (3). If read-out protection is off, as on the units documented so far, SWD
   is a second recovery path and a way to read this radio's own stock image out whole. Note that on the 578 PA13 and
   PA14 carry another device, so a first boot with a debugger needs a build that leaves that device alone; check
   whether this radio does the same before relying on SWD while running.
6. **Back up the factory calibration in the NAND** before anything writes to it: on the 868 the primary copy is at
   0x02F60000 with a fallback at 0x02F80000, about 1.5 KB including the per-MHz power tables the TX power DAC is
   driven from, and the settings records are at 0x02501000 and 0x02521000 (4.4). **No reflash of stock firmware
   brings this back.** Whether the vendor protocol reads NAND addresses over USB is open (question 10); if it does
   not, the backup waits until stage 3 and nothing may write to the NAND before it is done.
7. **Leave the baseband's own flash and the daughter board alone.** The SCT3258 has a separate update mode (`#` + the
   top key) and a vendor tool; the BT/APRS board has its own firmware. The port needs neither written, and the
   baseband is the one part whose recovery depends on a tool that is not ours.

Run the first boots with the antenna on a dummy load and no PTT. Transmit only from the 578 page's stage 7 on, at the
lowest power, and HIL tests that key the radio need `--allow-rf`
([tests/hil/README.md](../../tests/hil/README.md)).

## The stages that differ

Everything not named here follows [d578uv-bringup.md](d578uv-bringup.md) unchanged.

| Stage | What is different here |
| --- | --- |
| 1, It boots | The vector table is at 0x08004000, not 0x08006000. USB is PA11/PA12 as on the 578, so the GD32 clock path is already proven; the hold line and power key are this radio's own (the 868 reads PA0 at start-up, 4.3) |
| 2, Display | The init sequence is recorded byte for byte ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.2) and is the 868's. Drive the backlight on PB0 through TIM8 channel 2's complementary output, not a plain GPIO |
| 3, Keys and power | A matrix, not ladders, and it shares the LCD data lines: check that keys still read while the screen is drawing, and that the display is undisturbed by a scan. No serial keypad microphone; the daughter board replaces it. Add the battery, which the 578 has no equivalent for |
| 4, FM receive | One AT1846S on the 3-wire bus (SCLK PB3, SDIO PB4, SEN PB5). **The band and front-end switching is on the chip's own GPIO pins, not the MCU's**: register 0x1F, two bits per pin, set up as 0x5AE2. Receive band select is gpio0/gpio4, complementary at 300 MHz, and both go low on transmit ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.3). The 578's QA4818N register work does not apply |
| 5, Side B | Drops: no second receiver. Broadcast FM stays (RDA5802N on I2C 0x10, 4.1), and the 578's `bc` diagnostics line covers it |
| 6, Baseband | Unchanged, and it is the milestone: the chip answers `84 A9 61 00 02 20 17 01` about 2 s after the block goes out, and `bb` reports `ok`. If it does, the whole DMR side is already written |
| 7, FM transmit | The TX power DAC is PA4, fed from the NAND's per-MHz tables by level and frequency, through two 124-step ramp tables. The PA gate is PC4 with PE6, then keyed per slot from PD0. The transmit path is picked on the RF chip's gpio5 (above 300 MHz) with gpio7 for 165-300 MHz ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.3) |
| 8-9, DMR | Unchanged. The 578 page's `burst` line counts the PA gate the baseband keys per slot, which on the 868 is PD0 (4.3); confirm this radio keys its PA the same way before any long transmission |
| 10, GPS | ATGM336H or T3RV1, on a USART at 9600 baud with NMEA (1); the 868 wires it to PA2/PA3 (4.1). Not every unit has one: `gps_present` |
| 11, Flash stub | A new entry route (PTT + PF3) and a new memory map; the 578's stub is the model |
| New | The BT/APRS daughter board: whether `drivers/input/bt_at` fits it, and whether its FSK receiver can serve APRS receive instead of RadioFoundry's own AFSK |

## Open items

| Item | Where |
| --- | --- |
| Every pin, on a radio: the map is from the firmware, not from hardware | [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4 |
| Read-out protection on a current production radio, and whether BOOT0 is reachable | question 8 |
| Whether the calibration can be read over USB before RadioFoundry is installed | question 10 |
| Whether the vendor update mode accepts any image, or checks a model identity | question 4 |
| What the four boot block modes select | question 5 |
| The eight the image did not settle: PA5, ADC channel 10, the LED colors, PE0, PE15, PB11, PA9, PC13, the PB2+PE1 power-on path, the daughter board's protocol | [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 6 |
| Whether the BT/APRS board speaks the protocol `drivers/input/bt_at` already drives, and whether its `CMX865` can serve APRS receive in place of the software demodulator | this page, the table above |
| Whether the 868 can share this board directory, with its smaller flash and RAM | the registry, when the board lands |
