<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AT-D878UVII PLUS bring-up checklist

Latest status and resumption instructions: [2026-11-15 session handoff](d878uv2-session-handoff.md).
Normal boot, all keys and the battery bar are now confirmed. The channel knob currently needs two clicks per
channel change. Earlier planning statements below are historical; use the handoff for current hardware status.

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

The 868 and 878 are close enough that they will probably share a board directory the way the family's two mobile
builds do, with the 868's smaller flash and RAM as the constraint.

## What the board does today

`make BOARD=d878uv2` builds: about 342 KB of the 1004 KB region and 92 % of RAM. **Nothing has run on a radio**,
and no line of the pin map has been seen on hardware.

| Part | State |
| --- | --- |
| Display, backlight | written. The orientation is a guess (stage 2); the backlight is on/off, not dimmed |
| Keypad | written, from the map in [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.1.1: 18 of 24 matrix positions, the digits, MENU, EXIT, the arrows, P1 and P2. PTT, PF1 and PF2 are direct inputs; PF3 shares PB2 with matrix column 1, so the decoder drops that column while PF3 is held. None of it has been pressed on a radio |
| Knob, battery, power key, LEDs | written. The power key's polarity is read from the firmware and a pull-up implies the opposite, so the board holds the radio on for the first 8 seconds whatever the pin says (`D878UV2_POWER_KEY_GUARD_MS`). Confirm the polarity, then build with the guard at 0 |
| NAND, the core's storage | written, in RadioFoundry's own blocks above everything the stock firmware writes |
| Factory calibration | read, never written. The transmit power tables drive the PA; without them the transmitter stays at a low floor |
| FM receive and transmit | written. Band and front-end switching through the RF chip's own GPIO register |
| DMR | the baseband's glue is written. Two values are the family's and not this radio's: `DMR_CALL_OPTION` and the voice set-up |
| GPS | written, enabled from the RF chip's gpio2 |
| Bluetooth and APRS daughter board | **not written**: its protocol over UART4 is not recorded |
| Privacy and AES, the hotspot, SFR | **not written**: they need the baseband's voice-frame bypass (modem-sct.md 7), which no board uses yet |

So this is a bring-up image, not a replacement for the radio's own firmware: the keypad alone makes that plain.

## What this radio changes

| Item | AT-D578UV (done) | This radio |
| --- | --- | --- |
| Form | mobile, two receivers (4x QA4818N), no battery | handheld, one AT1846S, a battery to measure |
| `APP_BASE` | 0x08006000 | **0x08004000**: the 4.00 image's reset vector is 0x08004C91 (3). The vendor bootloader below it is never written |
| MCU | GD32F303VKT6 (3 MB flash), or AT32F435/437 on late units | **GD32F303VGT6**, marked on the board: 1 MB flash, 96 KB RAM. The same RAM, and no less usable flash: the 578 caps its image at 1 MB anyway (below) |
| Update mode | MENU + EXIT at power-on | **PTT + PF3** at power-on, the red LED blinking (3) |
| Baseband | SCT3258 on a bit-banged parallel port | the same chip and driver. The 868's host port is data PD8-PD15, HCSN PD2, HWRN PD3, HRDN PD4, HOBIB PD5, RSTN PD6, the PIO1 strap PC12, enables PA8 and PC6 (4.1) |
| Boot block | from the stock `.CDD` into the modem boot slot | unchanged: the same 2048 bytes, confirmed in the 878UVII PLUS 4.00 image (2.2), found by the same prefix search |
| Display | 160x128, MADCTL 0xC8 | the same controller and the same frame buffer: ST7735S-class, the 868's init value for value, on the same pins, so one `st7735` 8080 transport serves both. The controller's own frame is 128x160 and the stock firmware draws it that way, but **the glass sits rotated in the housing**, so RadioFoundry uses it as 160x128 and the MADCTL needs the row/column exchange the stock value does not set ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.2). The 868's RD strobe on PE15 does not exist here |
| Keypad | two analog ladders plus a serial keypad microphone | a key matrix on the LCD data lines PE7-PE14 with **three** sense lines (PB1, PB2, PC5), plus direct keys wired to ground: **PTT on PA10**, PF1 on PE1, PF2 on PA14, and **PF3 on PB2, the same pin as matrix column 1**. PA13 is not a key but an accessory detect ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.1.1, 4.3) |
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

## Building and flashing it

The build merges the DMR baseband's boot block from the owner's own stock firmware: drop the vendor `.CDD` into
`codecs/` (everything there is git-ignored) and `make` writes the merged image beside the plain one.

```
cp D878UVII_V4.00_20250725.CDD codecs/
make BOARD=d878uv2
# build/d878uv2/rfoundry_d878uv2.bin         the image alone, FM only
# build/d878uv2/rfoundry_d878uv2_codec.bin   with the boot block in its slot: the one to flash
```

Before the first write, in this order:

1. **Back up the codeplug with the stock CPS.** A vendor update ends with "MCU reboot" (PTT + PF1 at power-on),
   which erases it.
2. **Prove the radio answers.** Hold **PTT + PF3** at power-on until the red LED blinks, then:
   `python3 tools/anytone_update.py --board d878uv2 --probe`. It writes nothing and names the model. The board
   expects `D878UV2`, which the 4.00 update set's own `.spi` also carries; anything else and the tool refuses,
   which is the protection working.
3. **Check the plan without writing:**
   `python3 tools/anytone_update.py --board d878uv2 --dry-run build/d878uv2/rfoundry_d878uv2_codec.bin`

Then write it, with the radio in update mode:

```
python3 tools/anytone_update.py --board d878uv2 build/d878uv2/rfoundry_d878uv2_codec.bin
```

### The way back

Keep the vendor update set together (`.CDD` with its `.CDI` and `.spi` beside it). From update mode:

```
python3 tools/anytone_update.py --board d878uv2 --restore D878UVII_V4.00_20250725.CDD
```

A failed or stopped write leaves the radio in update mode and the same command writes again. The vendor
bootloader below 0x08004000 is never written by either tool, so update mode survives a bad image.

### What a flash cannot damage

The factory calibration sits at NAND block 379. RadioFoundry's NAND write window starts at block 1792 and the
driver refuses anything outside it, so neither the firmware nor an install can reach the calibration. The vendor
update route writes program flash only and does not touch the NAND at all.

**Proven on a radio.** Update mode was entered with PTT + PF3, the radio answered model `D878UV2`, hardware 0,
protocol `V100` (the GD variant), and a whole vendor set was written back over itself: 931,392 bytes in 29,106
frames, installed, restarted clean. So the way back is not a plan but a procedure that has been run. What has
**not** run on a radio is a RadioFoundry image itself, and `[flash.stub] tested = false` still holds for the
separate stub route.

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
| 2, Display | The init sequence is recorded byte for byte ([hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.2) and is the 868's. **Check the orientation first**: the board uses the panel in landscape with `madctl_xor` 0xC8, which is the correction another 160x128 board needed, not a value read from this radio. A picture that is rotated, mirrored or has red and blue swapped is that one byte in `display_d878uv2.c`. Drive the backlight on PB0 through TIM8 channel 2's complementary output, not a plain GPIO |
| 3, Keys and power | A matrix, not ladders, sharing the LCD data lines: check that keys still read while the screen draws, and that a scan leaves the display undisturbed. Press every key against [hardware-d878uv2.md](../spec/hardware-d878uv2.md) 4.1.1 and watch the diagnostics `keys` line. Check that **holding PF3 does not make P1 or P2 report**: they share PB2, and the decoder is meant to drop that column while PF3 is down. The power key's polarity is behind an 8 second guard until confirmed |
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

## Early startup probe

A first RadioFoundry install left the screen dark and exposed no USB device. Extending the PA0 shutdown guard
with `D878UV2_POWER_KEY_GUARD_MS=4294967295u` did not resolve it. The owner confirms a volume potentiometer with
a mechanical on/off click; the power-key interpretation of PA0 remains unconfirmed.

Build the isolated probe with:

```
make BOARD=d878uv2 BUILD=build/d878uv2-startup-probe D878UV2_DEFS=-DD878UV2_STARTUP_PROBE=1
```

Flash the resulting `_codec.bin` through update mode. This probe asserts PC3 and lights the receive LED before
clock setup, then initializes the display and draws the boot splash. A blinking receive LED means it passed
those calls. A steady receive LED means it reached the early marker but has not reached the blink loop. No LED
is inconclusive because the LED wiring is also unconfirmed. The watchdog can restart a stalled probe.

The probe stops before the scheduler, NAND, calibration, input, USB and modem initialization. No USB device is
expected from this image. It does not test normal application startup. Turn the knob off to end the test.

### Probe 1 result and probe 2

The owner observed the blinking receive LED and a RadioFoundry splash rotated 180 degrees. The early clock,
display and backlight path therefore runs. MADCTL changes from 0x68 to 0xA8 to reverse both axes; confirm the
corrected orientation on the next boot.

Probe 2 (`D878UV2_STARTUP_PROBE=2`) continues after the early splash into normal startup. Keep the extended
PA0 guard for this test. The green LED pulses before each operation:

| Pulses | Next operation |
| --- | --- |
| 2 | NAND probe |
| 3 | Storage mount |
| 4 | Factory calibration read |
| 5 | Modem initialization |
| 6 | GPS initialization |
| 7 | Return from board initialization to application startup |

No pulses after the splash narrows the failure to the path before the startup task reaches board initialization.
Repeated groups can indicate watchdog resets. Unlike probe 1, probe 2 can expose normal application USB if startup
reaches it. Record the last complete group, any repeated sequence, and whether the splash disappears.

### Probe 2 result and crash probe

The owner observed seven pulses followed by a restart. Board initialization returned; the reset occurs later or
in another task. Probe 3 (`D878UV2_STARTUP_PROBE=3`, with the extended PA0 guard) checks the retained crash record
immediately after the early splash. If its CRC is valid, it displays the summary, LR, CFSR and HFSR and stops
before the scheduler. Record every displayed line. If no valid record survives, it continues like probe 2;
a subsequent captured crash can be displayed on the next automatic restart. A hardware watchdog reset without
a captured record, or a bootloader that overwrites retained RAM, may still repeat without showing details.

### GPS startup fault

Probe 3 displayed `fault 3 at 20001E50 in gps`, LR `08032739`, SR `00020000 40000000`.
The crash-probe ELF maps LR to the indirect bus write in `at1846s_write_force`. GPS power used that bus before
RF initialization. GPS task creation moves from board initialization to the end of RF initialization.
The next image uses normal startup, with the extended PA0 guard retained. Confirm the splash, a working home
screen and USB, then leave it running beyond the GPS probe window. Probe stage 6 is removed because GPS creation
now occurs later. `rfGpsStartupTest` exercises the real RF initialization and immediately calls GPS power when
the GPS task would be created; it fails without the fix.

### Normal boot and controls image

The owner confirms normal boot with the GPS ordering fix. Battery was 0 percent, volume produced no bar,
shutdown at the knob click was ignored, the channel knob did not work, and only numeric keys 7, 8 and 9 responded.
The controls image calls power initialization, samples encoder edges with a valid rest mask and persistent
consumer count, and gives the keypad 5 microseconds to settle while preserving the interrupted LCD data byte.
It uses the normal 8-second shutdown guard, not the extended diagnostic guard.

After installing `build/d878uv2-controls/rfoundry_d878uv2_codec.bin`, check:

1. Battery millivolts and percentage. Compare the raw `board.adc.11` with the scaled value.
2. Volume knob travel and the existing volume bar. `board.adc.12` must change with the knob.
3. All numeric keys, then menu, arrows and exit. Hold a failing key while reading `board.matrix.*` and
   `board.matrix_idle`; do not change the map from a guessed position.
4. Ten channel clicks each way, with the manual encoder HIL test. Confirm direction and one event per click.
5. After at least 8 seconds on, turn the volume knob past the off click and allow 2 seconds for debounce.
   If it stays on, inspect `board.power_switch` rather than guessing its polarity.

`boardPowerStartupTest` verifies power initialization precedes other board work. `matrixEncoderTest` exercises
every mapped key against delayed sense inputs, output restoration, encoder edges between scans and consuming
each detent only once. Both encoder regressions and missing power initialization fail without their fixes.

### Controls image result

The owner confirms all keys work, the battery bar works, and the channel rotary works. Two physical clicks are
needed for one channel change with the current four-transition decoder. The likely next correction is two
transitions per detent with `QUAD_REST_EQUAL`, after checking physical rest states and raw transition counts.
No encoder-ratio correction has been applied yet. Volume bar/audio and shutdown still await explicit confirmation.

### Audio-path and codec-readback image

After the receiver-restoration fix, RSSI returns after transmit, but the owner reports silent FM and DMR receive
and no recognized DMR transmission at the other radio. The live DMR receive counters showed three calls and
118 voice bursts with no lost frames. The current patch restores the RF chip's microphone modulation source,
reapplies board audio trims after bandwidth changes, selects DMR-TX for digital transmission, and retunes the
receive frequency at transmit end. The receive audio fault remains open.

Build and flash `build/d878uv2-audio-path/rfoundry_d878uv2_codec.bin` in factory update mode, then power-cycle
normally. Read `tools/rfoundry_debug.py --port /dev/ttyACM0 view --section board`. The `board.codec.*` keys hold
actual startup register reads; -1 indicates an unsupported or failed read. Compare DAC power, HPL power,
HPL routing and mute state before changing gain values. Codec reads occur before the runtime packet engine,
so they need no ongoing debug-link transactions with the codec and do not trigger RF.

Host tests cover the four RF-path regressions, raw-I2C page framing, fragmented reads, maximum-length replies,
short replies, errors, timeouts and clock wrap. `test_codec_boot_snapshot` is an unattended HIL readback check;
it has not run on this image yet. Audio, actual transmit frequency, deviation and PA6 tuning remain unverified.

### Audio/FM/dual-watch image

The owner reports audible DMR receive after the RF-path fixes, recognized DMR transmit without microphone audio,
and no FM audio either way. A DMR channel selected at boot says "DMR modem off" until a channel change.
Raw codec snapshots were all -1; do not interpret them as mute or power-register values.

The next image is `build/d878uv2-audio-fm/rfoundry_d878uv2_codec.bin`. It adds microphone input routing, explicit
unity digital gains, shared SCT analog-call control for FM, DMR startup deferred until baseband boot completes,
and software dual watch. The codec/baseband now also boot with `CONFIG_DMR=n` when FM is enabled.

After flashing and powering on:

1. Start on a saved DMR channel. It should become active without changing channels.
2. Confirm DMR receive still has audio. Check microphone audio in an owner-controlled TX test.
3. Select FM and read the board view: `board.fm.enabled=1`, `board.fm.ready=1`, `board.fm.error=0`.
   Record pending field/error/timeout values if setup fails. Check FM receive audio before a TX test.
4. Enable dual watch with two channels and confirm it alternates, holds a received call and resumes afterward.
   Mixed FM/DMR timing needs a separate check; it is not simultaneous reception.

Host tests cover deferred DMR start and cancellation, the real codec setup profile, analog RX/TX command
sequences, short-press cancellation, mode ownership, errors and timeouts. HIL checks for DMR boot activation and
FM baseband readiness are added but have not run on this image. No RF tests were run by the agent.

### Vocoder/FM follow-up image

The owner confirms DMR startup and dual watch are working, with DMR receive audio still working. Microphone
speech on DMR and FM audio both ways remain unresolved. The FM engine reports error 6 at its first process-mode
command. `build/d878uv2-vocoder-fm/rfoundry_d878uv2_codec.bin` selects the analog vocoder before analog setup,
restores AMBE+2 when leaving FM, and waits for asynchronous mode loading before scanning on. It also adds
the restricted microphone input selector documented on the hardware page.

After installing it, read the board view on an FM channel. Record `board.fm.ready`, `.error` and `.last_field`.
Then return to DMR and confirm `board.fm.digital_ready=1` and DMR receive audio still works. Microphone input
routing can now be compared between IN1L and IN2L while the owner tests speech; the agent must not key RF.
The default remains IN1L. Codec snapshots still provide no usable data.

Host tests cover vocoder command ordering, its longer timeout, restoring the digital library, draining a pending
analog selection before digital selection, scan pause while busy, and rejection of microphone-route changes
while transmitting or outside the two allowed values. These tests do not confirm hardware audio.

### Vocoder image live check

On the installed vocoder/FM image, a receiver-only switch from the saved DMR channel to the adjacent FM channel
completed all six setup commands: `board.fm.ready=1`, error 0, timeouts 0, analog loaded 1. Returning to the saved
DMR channel restored vocoder 4 and `dmr.active=1`; digital ready was 1. The agent restored the original channel
and did not transmit. This confirms command acceptance and mode restoration, not audible FM audio.

The owner still needs to compare speech input routes. The agent requested IN2L (chip 1, page 1, register 0x34,
value 0x20) while idle for the next owner-controlled speech test. It is temporary; reboot restores IN1L (0x80).
The volume read zero during this check; raise it before judging receive audio. Actual codec register readback
remains unavailable.

### Latest receive/transmit result

FM receive is now confirmed audible by the owner. DMR receive, startup and dual watch also work. IN2L did not
produce FM transmit speech; DMR transmit was not recognized by the other handheld, so microphone audio on DMR
cannot be assessed yet. The agent restored IN1L without reflashing. A built-in-microphone FM test with the
programming/audio cable unplugged is pending to separate input routing from jack contacts/accessory behavior.

The DMR TX audit also found no use of the declared SCT slot-output pin and no per-burst PA gate: the PA is held
for the whole call. Implement shared burst gating and verify timing rather than copying measured delays from
a different RF path. Frequency and deviation still require measurement. No RF was triggered by the agent.

### Owner-operated FM transmit diagnostic check

With IN1L still requested, the owner reports no FM transmit speech. Before the test the radio was idle in DMR,
with 78 analog commands and no timeouts. After the owner selected FM and spoke while holding PTT, the core
recorded a 4,451 ms transmission on 433.500 MHz. The analog engine had 89 commands, ready 1, pending 0,
error 0, timeouts 0 and last field 25 (CARRIER_READY), and the radio was back in receive. The eleven-command
increase matches six FM setup commands plus TX work/start and release stop/RX work/carrier. This supports
completion of the command sequence, but is not a packet trace or proof of microphone signal or RF modulation.
The polling window ended before the manual test; these are before/after readings. The agent did not transmit.

All codec snapshot values remain -1. ADC power/mute, microphone bias and the line-output signal therefore
remain unmeasured. Whether the latest test used the built-in microphone with the accessory jack empty is
not explicitly confirmed. Do not treat successful command acknowledgments as proof of working audio.

### Built-in microphone still silent; next isolation test

The owner confirms the same carrier-without-speech result with the harness unplugged. The next image adds
an idle-only codec loopback, behind CONFIG_DIAG, rather than changing more gains. The engine has host tests
for its exact documented command, no call start, restoring RX and cancellation on retune. The board test
checks that RF TX is refused while loopback is selected.

On an FM channel, with scan and dual watch stopped, set low volume and open monitor. Keep PTT released.
Enable the loop byte using `python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 2 0x18 0x11 --page 0`.
Read `board.fm.loopback=1`, `.ready=1` and `.error=0` before judging audio. Speak into the built-in microphone.
Restore with `python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 2 0x18 0 --page 0`, or change channel.
Hearing speech would establish microphone capture through the codec; silence with an accepted loopback
would leave microphone setup or loopback support open. This does not test RF modulation. No agent RF TX.

Built `build/d878uv2-mic-loop/rfoundry_d878uv2_codec.bin` (1,030,144 bytes). Normal, CONFIG_DIAG=n,
CONFIG_FM=n (with its battery-save dependency off), and CONFIG_DMR=n builds passed without compiler warnings.
Analog sequencing, board RF guard and codec-setup host tests passed; analog sequencing also passed with
CONFIG_DIAG=0. Documentation links and generic checks passed. The opt-in HIL loopback test was added and
syntax-checked, but not run: the connected radio still has the previous image. No hardware loopback or audio
result is claimed. Temporary configuration build trees were removed.

### Codec loopback owner result

On the microphone-loop image, scan was stopped, the adjacent FM channel selected, volume set to step 2 and
monitor opened. The loopback command completed with loopback 1, ready 1, pending 0, error 0 and state idle.
The owner reported very loud unusual squeaking, not intelligible microphone speech. The agent immediately
set volume 0, closed monitor and disabled loopback. Readback then showed loopback 0, ready 1, pending 0,
error 0, state idle and monitor 0. The radio remains on FM with the speaker muted. No RF was requested.

This is not a successful microphone-capture result. Acoustic feedback, an unintended internal loop or a
mode-specific implementation of the documented loopback remain possible; no cause was measured. Do not
repeat with the speaker open or treat command acceptance as validation of the diagnostic's signal path.

### Owner reports an existing frequency correction

The owner recalls needing FQCU approximately 3620 to match frequency. The original interface, band and units
are not yet confirmed. Current calibration loading reads only the transmit power tables; PA6 tuning is not
implemented and no FQCU correction is applied by this port. This makes oscillator alignment a concrete lead
for the unrecognized DMR transmission, but there is no carrier-frequency measurement for this image yet.
Do not interpret 3620 as hertz or program it into a DAC/PWM without establishing its stock meaning. The dirty
spec-writer has been asked to trace the setting's storage and hardware destination. No radio changes or RF.

### Saved DC-control output bring-up

The dirty-side audit established little-endian calibration offset 0 -> PA5/DAC2, shared across bands.
This output had never been initialized by the port. The new image reads its own saved 12-bit count and
applies it at RF initialization and retuning. Invalid data falls back to the documented calibration-reset
count 1900; this is an explicit RadioFoundry policy, not a proven stock invalid-data fallback. Calibration
storage stays read-only. Host tests cover primary/mirror selection, raw 3620 decoding, 0/4095 endpoints,
invalid or unreadable words, failed power tables, stale reload state and independent PA/control DAC writes.

After flashing, read `board.control_dac.count` and `.factory`. A factory flag 1 establishes that a saved word
was read, not that the carrier is aligned. Check receive first; an owner-controlled DMR call and a measured
carrier offset are still needed. No FM speech fix is claimed and the squealing loopback will not be repeated.

The resulting image is `build/d878uv2-control-dac/rfoundry_d878uv2_codec.bin`, 1,030,144 bytes. Normal,
CONFIG_DIAG=n, CONFIG_FM=n (battery save off) and CONFIG_DMR=n builds passed without compiler warnings.
Factory-reader and RF/DAC host tests passed; docs links and HIL syntax checks passed. The read-only HIL
calibration test is added but not run until this image is installed. No carrier-alignment result is claimed.
Temporary configuration build trees were removed.

### Combined TX-path image, held for owner testing

At the owner's request, the DAC-only image is not the next proposed flash. The combined image adds the
stock-audited RF control profile, FM TX filter and microphone re-enable ordering. Host tests check custom
and default control profiles through real driver operations, narrow/wide FM and DMR state values, receive
restoration, mic codec ACKs before work mode/call start, refusal and early-release cancellation. The actual
board audio profile is checked for IN1L followed by unmuted left PGA. No audible mic success is claimed.

Combined image: `build/d878uv2-tx-path/rfoundry_d878uv2_codec.bin` (1,030,144 bytes). Full host suite passed.
Normal, CONFIG_DIAG=n, CONFIG_FM=n (battery save off), CONFIG_DMR=n, and a representative default-control
board build passed without compiler warnings. The shared analog tests also passed with CONFIG_DIAG=0.
Documentation and generic checks passed. Temporary configuration build trees were removed. No hardware
mic test, RF transmission or flash was performed by the agent; this image supersedes the DAC-only candidate.
The microphone cannot be reported working until the owner tests this combined image.

### Regression on combined TX-path image

The owner reports unchanged silent FM TX and lost DMR RX/TX, with occasional unexplained receive blips.
Live readings: control count 1900, factory 0; mic_writes 2, analog error/timeouts 0; DMR received calls 0.
After a receive-only switch to DMR, active 1 and digital ready 1. The blips are not established as PC spurs.
The unverified 1900 DAC fallback is removed. Missing calibration now leaves the control DAC untouched.

A corrected stock audit found that the reader had selected the retirement marker as if it meant active,
and had ignored the persisted NAND block map. The reader now validates the map fingerprint/log, follows
read-only block replacements and selects an active primary or active mirror. No map reconstruction or
writes are possible. Host tests cover retired-copy rejection, relocated calibration, malformed map bounds,
duplicate entries, log selection, cross-block reads, stale load state and no DAC initialization without a
valid saved word. These correct demonstrated reader bugs; hardware recovery is still unverified.

### Full TX trim and calibration repair candidate

The next candidate combines the corrected read-only calibration reader with the complete stock-audited TX
trim (0x0A masked operations, FM 0x44=0x09FF / DMR 0x0BFF, 0x41=0x070F, 0x59=0x0C50). These TX writes
were missing even after microphone-route writes were acknowledged. RX values are saved and restored explicitly
because a cached mode table need not rewrite those registers. The host test covers release and direct RX entry.
No owner speech or DMR recovery result exists yet for this candidate. The connected radio remains on the prior
TX-path image; the agent left it idle on the saved DMR channel with scan stopped, and did not transmit.

Final candidate is `build/d878uv2-cal-tx/rfoundry_d878uv2_codec.bin` (1,030,144 bytes), superseding the
standalone calibration-reader and DAC-only candidates. Full host suite, normal/DIAG-off/FM-off/DMR-off builds,
generic/doc checks and static stack check passed without compiler warnings. The UI stack is now 544 words:
the checker found the prior 526-word allocation eight bytes short of its required safety margin. Every task
fits after adjustment. Temporary configuration and superseded reader/DAC build trees were removed; the
previous installed TX-path image and earlier working receive image are retained for comparison.

The owner has not installed this candidate yet. After installation, read `control_dac.map_status` (4 expected),
`.map_entries`, `.factory` (1 expected) and `.count` before any TX test. A valid map and saved word are not a
carrier measurement or proof of microphone audio. No agent RF TX or flash was performed.

### Owner refines audio result before committing

On rechecking the installed image, the owner can hear FM microphone audio, but it is very quiet. This
supersedes the earlier description of entirely absent FM speech. No measured level comparison exists, so
whether earlier images already passed quiet audio is unknown. Low overall TX gain is plausible; it is not
a demonstrated failure of microphone capture. The complete stock TX trim in the new candidate remains
hardware-unverified and should be checked before changing codec gains further.

The owner also reports "TS1 late" during DMR receive and no recognized DMR transmission on the peer DM-32.
This indicates some receive-side activity, not a confirmed complete call or correct frequency alignment.
The latest candidate's calibration map/count and the actual carrier offset still need checking.

A read-only check after this report confirms the owner has already installed the new map-aware candidate:
`map_status=4`, `map_entries=3`, `map_crc=3503888722` (0xD0D91952), `factory=0`, `count=0`. Thus the saved
map loads, but a valid saved DAC word has not been selected. The latest quiet-FM/late-DMR observations must
be associated with this candidate, not assumed to describe the prior TX-path image. No setting or RF change
was made by this check. Calibration selection remains open despite the successful mapping-layer read.

### Native mic-gain control and calibration inspection

The new work connects the existing Mic gain setting to the board's codec path. It previously had no board
hook, so raising the menu value could not affect native FM/DMR microphone capture. Gain is stored and applied
on the next transmission; the default stays unchanged. Host tests check half-dB conversion, saturation, actual
voice-setup packets and the board hook. A no-RF HIL test checks the setting/diagnostic path and restores it.

Read-only calibration inspection is chip 3: byte-read pages 0/1 for each copy's first 256 bytes, 2/3 for its
eight marker bytes. The next installed image can report the raw DAC words, audio calibration bytes and both
marker sets without another firmware edit. It refuses reads while keyed and cannot write factory data.
Host tests cover mapped reads, limits, missing maps and the keyed guard. Saved calibration selection is still
strict; no guessed DAC value or unvalidated mirror is applied.

The installed image reports Mic gain step 5 and hardware hook 0, confirming the setting did not reach the
native audio path. The new hook makes step 8 request +6 dB (PGA 0x20) relative to the unchanged default.
Pages 4..7 of the read-only calibration inspection add an ECC-failure byte before the same windows, so
normal-read errors can be investigated without silently accepting failed-ECC data. No calibration selection
or DAC override is introduced. The owner must still compare speech level and decoding after installation.

Candidate: `build/d878uv2-mic-control/rfoundry_d878uv2_codec.bin` (1,030,144 bytes). Codec-profile, RF-board,
factory-reader and remapping host tests pass. Normal, DIAG-off, FM-off (battery save off) and DMR-off firmware
builds pass without compiler warnings; static stack check passes. Doc/generic/license checks pass. HIL tests
were syntax-checked but not run; pytest is absent from the available Python environment. Temporary build
configurations were removed. The agent did not flash, transmit or alter the owner's gain setting.
