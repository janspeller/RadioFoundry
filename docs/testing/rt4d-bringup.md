<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# RT-4D bring-up checklist

The RT-4D port (`make BOARD=rt4d`) builds; nothing has run on a radio yet. This is the order the bring-up follows,
what each step proves and what to change when it fails. Every step names the source knob the likely failure points
at. Hardware facts: [hardware-rt4d.md](../spec/hardware-rt4d.md).

The feature checklist with each feature's HIL tests and last result is `tools/bringup.py list --radio rt4d`;
`tools/bringup.py run --radio rt4d` records a run ([tests/hil/README.md](../../tests/hil/README.md#bring-up-mode)).

## 0. Before anything

1. **Back up the data flash** with a tool that reads it (the stock CPS or a reader on the 16 MB SPI NOR). The port
   reads the factory calibration from its first sector (3.2) and never writes below 0x700000, but a backup is the
   only way back from a mistake.
2. Check that the vendor bootloader answers with the stock firmware: hold **PTT** while switching the radio on (6). It
   is the recovery path for every step below. The bootloader below 0x08002800 is never written.
3. Build and check the image without a radio:

   ```
   make BOARD=rt4d
   tools/block_loader.py --model rt4d build/rt4d/rfoundry_rt4d.bin --dry-run
   ```

4. Load it in bootloader mode: `tools/block_loader.py --model rt4d build/rt4d/rfoundry_rt4d.bin`. Untested on a
   radio: whether the bootloader checks anything in the image before it runs it is open (7.3).

Run the first boots with the antenna on and **no PTT**. Transmit only into a dummy load, from step 6 on.

## 1. It boots

| Look for | Proves | If not |
|---|---|---|
| Backlight on, then the Home screen | Reset handler, `at32_clock_init`, the RTOS tick, the ST7567 bus | Nothing: the image may not start at the vector table (`arm-none-eabi-objdump -h`), or the bootloader refuses it. A blank but lit panel: `display_rt4d.c` (bit timing, `regulator`, `contrast`) |
| Picture the right way up, not shifted | `com_reverse`, `seg_reverse`, `col_offset` | `display_rt4d.c` `panel` |
| Readable contrast | electronic volume | `panel.contrast` (0x12, the stock value before its settings load; the stock range is 0x05-0x23) |

### The boot probe

If nothing shows, build the probe image. It blinks the start-up stages on the backlight (PA3), with the watchdog off:

```
make BOARD=rt4d RT4D_DEFS=-DRT4D_BOOT_PROBE BUILD=build/rt4d-probe
tools/block_loader.py --model rt4d build/rt4d-probe/rfoundry_rt4d.bin
```

The backlight comes on at reset entry. After each stage it goes dark N times (250 ms dark, 250 ms lit), shows the
pattern twice, then stays lit for a 1.5 s pause. Stage 1 runs before the clock is set, so it can blink faster or slower
if the bootloader left another clock. A full boot takes about a minute.

| Pulses | Stage passed |
|---|---|
| 1 | vector table, FPU, `.data`, `.bss` (1 s pulses: the bootloader entered in handler mode; recovered) |
| 2 | `board_rf_safe` |
| 3 | the watchdog point (the watchdog is off in this build) |
| 4 | `at32_time_init` |
| 5 | `board_early_init`: pins and `at32_clock_init` |
| 6 | `app_start`, just before the scheduler |
| 7 | the first task runs (`board_init` entry) |
| 8 | `board_init` done: power, data flash, calibration |

| Seen | Means |
|---|---|
| Dark from power-on | the image never reached the reset handler |
| Lit and still after the last burst | stuck in the stage after it |
| Fast blinking (100 ms) | a fault or an interrupt without a handler |
| Slow blinking (500 ms) | an RTOS stack overflow or a failed allocation |
| Starts over at stage 1 | a reset |

## 2. The debug link on the programming cable

USART6, 115200 8N1, on the K1 cable.

```
tools/rfoundry_debug.py --port /dev/ttyUSB0 version
tools/rfoundry_debug.py --port /dev/ttyUSB0 screenshot -o /tmp/rt4d.png
python3 -m pytest tests/hil -v -rs --radio-port /dev/ttyUSB0 -k "health or boot or ui"
```

| Look for | Proves | If not |
|---|---|---|
| `board=RT-4D`, `display=128x64x1`, features with `HOSTLINK,DEBUGLINK` and no `USB` | USART6, its interrupt, the clock tree | Garbage: the baud divisor (`uart.c`, PCLK2 / 115200) or the system clock not at 72 MHz (`clock.c`) |
| `tasks.heap_min_free` above 1 KB after a few minutes of use | the RTOS heap fits | `configTOTAL_HEAP_SIZE` in `boards/rt4d/board.mk` (14,336 B); RAM is 99.6 % used |

## 3. Keys, PTT, LEDs, battery

| Look for | Proves | If not |
|---|---|---|
| Every key in the debug link's key view | the matrix and its orientation | `input_rt4d.c` `map` (KEYOUT by KEYIN) |
| A/B and both side keys | the direct keys on KEYIN1-3 | `direct` in `input_rt4d.c`; which side key is SK1 is a guess |
| PTT down and up, also with the cable plugged and idle | the shared-line gate | `drivers/input/sharedptt` |
| Battery voltage near a meter's reading | the scale `raw × 400 / 66` (inferred) | `power_rt4d.c` `BAT_MV_NUM` / `BAT_MV_DEN` |
| Red and green LEDs | PA13 / PA14 | Built with `RT4D_DEFS=-DRT4D_KEEP_SWD` they stay off |

## 4. Data flash and calibration

| Look for | Proves | If not |
|---|---|---|
| The flash's JEDEC ID in the debug link | SPI2, IOMUX 5, the chip select on PB12 | `board.c` `flash_cfg` (`mdiv`, mode 0) |
| Settings survive a power cycle | the settings ring at 0x711000 | `board.c` `flash_map` |
| Transmit allowed on a channel in band | the calibration records read and plausible | `cal_rt4d.c` `plausible()` |

## 5. FM receive

Tune a known FM signal (a repeater, a weather broadcast) on VHF and on UHF.

| Look for | Proves | If not |
|---|---|---|
| RSSI rises on a carrier, the squelch opens | the BK4819 bus, its init, the band's RX path (GPIO2 / GPIO5) | `radio_rt4d.c` `init_table`, `start_rx` |
| Audio from the speaker | PB10 low, PF8 low, PA2 high, REG_47 = 0x6140, REG_48 = 0xB32A (`board.*` view keys, hardware-rt4d.md 6.3) | `speaker()` in `radio_rt4d.c` |
| On frequency (an SDR beside it) | the reference offset applied to receive too | `rt4d_cal_freq_10hz`: receive may need the plain frequency |

```
python3 -m pytest tests/hil -v -rs --radio-port /dev/ttyUSB0 -k "fm and not tx"
```

## 6. FM transmit (dummy load, power meter, SDR)

Only with `--allow-rf` ([tests/hil/README.md](../../tests/hil/README.md)), into a dummy load.

```
python3 -m pytest tests/hil -v -rs --radio-port /dev/ttyUSB0 --allow-rf -k "fm_tx"
```

| Look for | Proves | If not |
|---|---|---|
| Carrier on the frequency, both bands | the TX state, the TX paths, PA10 | `tx()` in `radio_rt4d.c` |
| Low and high power differ as rated (about 1.5 / 5 W) | which REG_36 table is which (3.2, inferred) | `CAL_OFF_BIAS_HIGH` / `CAL_OFF_BIAS_LOW` in `cal_rt4d.c` |
| Microphone audio on the SDR | REG_30 bit 2 in transmit | `RT4D_TX_STATE` in `radio_rt4d.c` (0xC3FA as stock; 0xC3FE turns the microphone ADC on) |
| No carrier after PTT up, and after a reset during transmit | `tx_off`, `board_rf_safe` | `board_rf_safe` in `radio_rt4d.c` |
| 1750 Hz burst (the tone key): a tone at 1750 Hz on the SDR, the microphone back after it (`test_fm_tx_tone_burst`, `test_fm_tx_tone_burst_key`). Untested | the BK4819's tone generator in transmit, and the TX state after it | `tx_tone` in `radio_rt4d.c`, `RT4D_TX_TONE_GAIN` for the deviation |
| Roger beep at PTT up, on the SDR (`test_fm_tx_roger_beep`). Untested | the same tone path at the end of a transmission | `tx_tone` in `radio_rt4d.c` |
| DTMF: the digits keyed during transmit decoded by a second radio or an SDR DTMF decoder, the microphone back after the last (`test_fm_tx_dtmf`, `test_fm_tx_dtmf_stop`). Untested | both tone generators, the stock DTMF deviation | `tx_dtmf` in `radio_rt4d.c` |

## 7. Frequency detect

No transmitting from this radio. A second radio, keyed by hand, is the signal.

```
python3 -m pytest tests/hil -v -rs --radio-port /dev/ttyUSB0 -k freq_detect
RFOUNDRY_HIL_FD_HZ=145500000 python3 -m pytest tests/hil -v -rs -s --radio-port /dev/ttyUSB0 -k freq_detect
```

| Look for | Proves | If not |
|---|---|---|
| Tools > Freq detect searches (`fdet=1`), Back stops it (`fdet=0`) | the hooks, the scan start and stop | `fd_start` in `radio_rt4d.c` |
| A radio keyed next to this one reads within one step, VHF and UHF | the raw reading and the reference offset (hardware-rt4d.md 3.1.1) | `rt4d_cal_detect_hz` in `cal_rt4d.c` |
| OK tunes the VFO to it, and the receiver hears the other radio there | the hand-off to the VFO | `fd_key` in `ui_menus.c` |

## 8. DMR through the HF6853

The call-level driver is built and on by default ([hardware-rt4d.md](../spec/hardware-rt4d.md) 7.2.1). What runs
today: the chip powers up, gets the channel, the key and the digital monitor (0x62), and receives and plays calls
itself; each call heard opens the speaker and goes to the receive screen and Recent, the signal level shows during
it, and PTT starts a call (0x06). Only `test_ptt` transmits from the RT-4D, and only with
`--allow-rf`. The other radio is the owner's second DMR radio, in
direct mode on the same frequency, color code and slot, into a dummy load or at its lowest power.

Flash and the first checks (a DMR channel needs a DMR ID in the codeplug):

```
make BOARD=rt4d
tools/block_loader.py --model rt4d build/rt4d/rfoundry_rt4d.bin      # PTT held at power-on
tools/rfoundry_debug.py view --section tasks                         # tasks.heap_min_free above 1 KB
python3 tools/rfoundry_debug.py --port /dev/ttyUSB0 --timeout 3 view --section board | grep -E 'amp_en|af_sw|dmrc'
python3 -m pytest tests/hil -v -rs --radio-port /dev/ttyUSB0 -k dmr_call_level
RFOUNDRY_HIL_DMR_PEER=1 python3 -m pytest tests/hil -v -rs -s --radio-port /dev/ttyUSB0 -k test_peer_call
```

| Step | Look for | If not |
|---|---|---|
| A DMR channel | `dmrc.ready=1` and `dmrc.fault=0` (`dmr` section) within a few seconds; `dmrc.state=ready`, `dmrc.give_ups=0` (`dmr_stats`) | `dmrc.state=wait` / `cool` and `dmrc.frames_rx=0`: no 0x1A report (the UART pins and IOMUX, PB1, the baud rate: `dmr_rt4d.c`). `dmrc.rx_bad` rising: the checksum or the framing (`hf6853_frame.c`). `dmrc.give_ups` rising: replies not matched (`hf6853.c`) |
| The start-up | `dmrc.seen` lists 1a and the start-up commands answered; `dmrc.version` not 0 | the 0x25 reply's layout (open) |
| Stays up | `dmrc.faults` unchanged over minutes | `dmrc.fault=2`: the link was lost |
| A group call from the other radio | Its audio on the speaker, the green LED on; the receive screen with the other radio's ID and the talkgroup, then Recent. During the call `board.amp_en=1` and `board.af_sw=1`, after it both 0. `dmrc.heard_raw` holds the raw event: the caller's ID in BCD (ID 1234567 as `01234567`) confirms the BCD reading, `dmrc.heard_not_bcd` rising says otherwise. The call ends when the other radio stops: `dmrc.heard_ends` rises (ended by 0x6F), `dmrc.heard_quiet` stays (the 30 s fallback). Note `dmrc.heard_gap_max_ms`, `dmrc.sr06`, `dmrc.last_report` and `dmrc.seen`, all in the `board` section as `board.dmrc.*` | No audio while `amp_en=1` and `af_sw=1`: the HF6853's volume, or its audio path (3, Receive audio). `amp_en=0` during the call: `dmrc.heard=0`, no 0x60 (`dmrc.heard_events`). No caller on the screen: `dmrc.heard_events=0`, the event's code (5.4.2). The call stays on the screen 30 s after the other radio stopped: no 0x6F came (`dmrc.heard_quiet` rose; `dmrc.sr06` shows the codes seen) |
| No buzz | On a quiet DMR channel, and while the RT-4D transmits (`--allow-rf`, dummy load): the speaker silent, `board.amp_en=0`, `board.af_sw=0` | `amp_en=1` with `dmrc.heard=0`: the audio hook (`rt4d_dmr_audio` in `radio_rt4d.c`) |
| Digital monitor | **Monitor** on the DMR channel: `board.dmrc.hear_all=1`, and a call from the other radio to a talkgroup outside the channel's (or a private call to another ID) plays. **Monitor** again: `hear_all=0`, and such a call stays silent. After each call `dmrc.hear_all` keeps its value (0x62 sent again after 0x6F) | `hear_all=none`: the chip didn't answer 0x62 (`dmrc.nacks`, `dmrc.last_report`). The call plays with the monitor off: the chip's own group filtering (5.4.1) |
| Signal level | With the other radio keyed: `board.dmrc.carrier=1`, `board.dmrc.rssi_raw` a number, and the receive screen and `rssi_dbm` show a level in dBm, never -160 during the call. Bring the other radio closer (or raise its power, into a dummy load): `rssi_raw` rises. Unkeyed: `carrier=0` and `rssi_dbm=-160`. Note `rssi_raw` at two or more known levels (a signal generator or a known attenuator): they calibrate the scale (7.3) | `carrier` stays 0 while a call plays: no 0x59 report (the level is still asked for during the call: `rssi_raw` changes). `rssi_raw` never changes: 0x05's reply (`dmrc.timeouts` rising, `dmrc.seen` without 05) |
| Back to FM | `dmrc.running=0`, `dmrc.state=off`; FM receive and transmit as in 5 and 6 | the BK4819 set-up after the hand-back (`set_mode` in `radio_rt4d.c`) |
| Encryption | With a key entered with the stock CPS and privacy on (**\***, **6**): `dmr.enc` and `dmr.key_id` show it, `dmr.key_loaded=1`; an encrypted call from the other radio with the same key plays, with another key it doesn't | `dmr.key_loaded=0`: the chip refused 0x81 (`dmrc.nacks`), or the stock record's layout (`key_get` in `dmr_rt4d.c`) |
| PTT on a DMR channel (`--allow-rf`, dummy load) | `state=tx`, `dmr.phase=tx_voice`, `dmrc.on_air=1`; the other radio shows the RT-4D's ID calling the channel's contact and plays the voice. Release: `dmr.phase=idle`, `dmrc.on_air=0`, no carrier on the SDR | `dmr.phase=tx_denied`, `tx_error=busy`: the chip answered 0x06 with "no channel" or "killed" (`board.dmrc.last_reply`). In every case note `board.dmrc.last_reply` and `board.dmrc.sr06` after the PTT: they settle which codes mean on the air (5.4.2). A carrier after release: power off at once, then report it (the PTT up, 5.4.2). The other radio shows a wrong caller ID: our own ID (`dmr.own_id`, 0x2A in 5.4.1); a wrong target: 0x06's (5.4.2) |

Later, once the open commands are known (hardware-rt4d.md 7.3): a talkgroup outside the RX group list not played,
SMS, radio check, emergency.

## Open items to settle on the bench

| Item | Where |
|---|---|
| ST7567 contrast and the panel's orientation | `display_rt4d.c` |
| Which side key is the upper one | `rt4d.h`, the key map |
| LED and backlight polarity | `power_rt4d.c`, `board.c` |
| The battery divider | `power_rt4d.c` |
| Whether receive takes the reference offset | `cal_rt4d.c` |
| The REG_36 tables and REG_30 in transmit | `cal_rt4d.c`, `radio_rt4d.c` |
| The heap's low-water mark | `configTOTAL_HEAP_SIZE` in `board.mk` |
| The fallback end of a call heard should the 0x6F be lost (30 s) | `HF6853_HEARD_QUIET_MS` in `hf6853.h` |
| The raw signal level against dBm (placeholder: raw 21 = -120 dBm, raw 57 = -60 dBm) | `HF6853_RSSI_*` in `hf6853.h` |
