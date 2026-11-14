<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# Frequency detect

Frequency detect finds the frequency of a nearby transmitter with the RF chip's frequency counter scan, shows it, and
tunes the VFO to it. It is for programming a channel from a radio held next to this one: a strong carrier at the
antenna, not a distant station. Which radios have it is in [feature-matrix.md](../feature-matrix.md).

## 1. When a radio has it

| Item | Value |
|---|---|
| Switch | `CONFIG_FREQ_DETECT` ([build-config.md](../build-config.md)); needs `CONFIG_FM`. Off by default; on for the radios whose chip has the scan |
| Board | `CAP_FREQ_DETECT` in `board.caps`, and `hal_radio_t.fd_ranges`, `fd_range_count`, `fd_start`, `fd_poll`, `fd_stop` (`hal/hal_radio.h`) |
| Core | `core/radio/freq_detect.c` (the state machine), `core/radio/radio_sm.c` (`RREQ_FREQ_DETECT`), Tools > Freq detect (`core/ui/ui_menus.c`) |

`radio_fd_capable()` is true only when the capability and every hook are there. A radio without them shows no Tools
row, and a request to search is refused with `no_hw`.

## 2. The board's side

| Hook | Contract |
|---|---|
| `fd_ranges` | The ranges the user picks from, low to high, both ends in Hz. The board searches one range as a whole |
| `fd_start(lo, hi)` | Set the receive path for the range, put the chip in receive if it isn't, start the scan. False: the board can't (not set up, transmitting) |
| `fd_poll(&hz)` | False while the chip has no reading. True with the frequency in Hz, after the board's own corrections (reference offset, calibration), not rounded |
| `fd_stop()` | Scan off. The core tunes the channel again with `rx()` afterwards, so the board needn't restore anything else |

The hooks run on the radio task only, never while transmitting. The shared BK4819 driver has the chip part
(`bk4819_freq_scan_start`, `_poll`, `_stop`): REG_32 = 0x0B01 starts the scan, REG_0D bit 15 set means no reading yet,
the raw reading in 10 Hz units is `(REG_0D & 0x07FF) << 16 | REG_0E`, and REG_32 = 0 stops it. The board adds its
corrections to the raw reading.

## 3. The radio core

| Step | Behavior |
|---|---|
| Start | `RREQ_FREQ_DETECT` arg 1, arg2 the range. Refused (`view.fd_refused`) without the hardware (`no_hw`), for a range past the list (`range`), while transmitting, PTT is held, the radio is a hotspot, an APRS job, a scan or dual watch runs (`busy`), on a DMR, AM or broadcast channel (`mode`), or when `fd_start` returns false (`board`) |
| Searching | `view.fd_on`. The squelch is closed and the speaker muted; battery saving is off; the poll skips the squelch, scan, APRS and VOX |
| Reading | Every `FDET_POLL_MS` (50 ms) at the radio's poll. A reading inside the range goes to `view.fd_hz` and counts in `view.fd_hits`. One outside it is dropped |
| Again | After a reading, or after `FDET_GIVE_UP_POLLS` (10) reads without one, the scan goes off and on again, so the search goes on until it is stopped |
| Stop | `RREQ_FREQ_DETECT` arg 0, or any other request that changes what the radio does (PTT, a channel, the VFO, monitor; not alerts, settings or the trim retune). The scan goes off and the channel is tuned again. `view.fd_hz` keeps the last reading |

## 4. The UI

Tools > Freq detect is the last row of Tools, after the numbered tools, without a number. It is there only on a radio
that has the feature. The screen (`freq-detect`):

| Element | Behavior |
|---|---|
| Open | Searches at once, in the range that holds the channel's receive frequency, else the first |
| Range | Up and Down (or the knob) step through the ranges; each step starts a new search |
| Frequency | The last reading, rounded to the VFO step; dashes until there is one. Under it: Searching..., or why it stopped, and the number of readings |
| OK | Tunes the VFO to the frequency shown (`radio_req_vfo_set`), which ends the search, and goes Home. Without a reading: a toast |
| # | Searches again from nothing |
| Back | Stops the search and closes the screen. Leaving it any other way (Home, PTT) stops it too |

## 5. Debug link

The core view section has `fdet` (1 while searching), `fdet.range`, `fdet.hz` (0: none since the start), `fdet.hits` and
`fdet.refused` (`none`, `no_hw`, `busy`, `mode`, `range`, `board`) ([debuglink.md](debuglink.md) 3.7).

## 6. Tests

| Test | What |
|---|---|
| `tests/host/radioFeaturesTest.c` | The state machine (give up, drop, keep, round) and the radio core: refusals, muting, the end by stop, PTT and the VFO |
| `tests/host/uiFlowTest.c` | Tools > Freq detect opens `freq-detect`, a range step, OK tunes the VFO, Back stops |
| `tests/hil/test_freq_detect.py` | On the radio, opt-in: a search starts and stops over the debug link; with a second transmitter keyed on a known frequency (the owner's), the reading is within one step of it |

## 7. Open questions

- The poll interval: 50 ms and ten reads before a restart is this port's choice; the stock timing isn't known.
- How close a reading must be: the chip's counter accuracy against a strong carrier is unmeasured.
