<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AT-D878UVII PLUS session handoff — 2026-11-16

Supersedes the 2026-11-15 handoff. The bring-up history is in [d878uv2-bringup.md](d878uv2-bringup.md); this file
is only what the next session needs to continue.

## Continuation: missing mode controls implemented

The current candidate is `build/d878uv2-mode-audio/rfoundry_d878uv2_codec.bin`. It adds mode-entry 0x6C and
12-byte 0x6B, using calibration +0x4A..+0x4E for known bytes and zero reserved trailing bytes. The ten-byte
startup store update is not the twelve-byte wire length. Digital setup uses the shared engine's existing
control-field classification; analog setup now accepts acknowledged board control fields. Per-call signaling
is retained. Existing ANALOG_CONFIG remains nominal rather than claimed stock RAM reconstruction.

Targeted host tests, four build configurations and stack checks pass. The owner flashed `e3acd148` and
reports no improvement in transmit speech. Do not describe 0x6B/0x6C as a proven microphone fix.
This candidate includes the earlier ramp; first test is 70 cm. Further dirty-side VHF/table findings need a
separate review. The sections below preserve the original handoff and hardware observations; use git log for
the latest implementation commit.

Read-only follow-up confirmed `e3acd148` on the radio. The FM engine reported zero timeouts and four completed
microphone writes. All codec boot-snapshot registers still reported -1, so successful command completion does
not establish converter, routing or output-power state. The owner also reports SQ5 opening on noise. The
shared software threshold is 52 noise counts at SQ5; the board currently supplies raw noise without bandwidth
normalization. Stock squelch uses bandwidth-dependent thresholds; verify the level mapping before changing it.

On FM 433.500 MHz, 12.5 kHz, SQ5, two read-only samples totaling 500 polls stayed idle with noise 56..63.
The intermittent false opening was not captured. RF register 0x48 was 0x19B1 and 0x49 was 0x0C15.
The dirty-side audit corrected an earlier swapped bandwidth label, but did not establish a mapping between
the stock hardware thresholds and the software noise scale. No noise-offset change is justified by that audit.

The owner-approved local microphone loopback reached ready with no command error, TX false and loopback true.
At fixed volume 1 the owner unplugged the cable to hear the internal speaker and heard faint white noise;
speech was too quiet to distinguish. This is inconclusive. USB removal prevented the cleanup commands, so
loopback and fixed volume must not be assumed restored. Reconnect and restore knob control before a repeat;
changing channel cancels loopback. No RF was initiated by the agent.

After reconnection, monitor and loopback were cleared and physical knob control was restored. A repeat local
loopback completed ready/error=0 with knob control retained; the owner unplugged, raised volume and reported
only hiss or silence, without speech. The agent then cleared loopback/monitor and restored knob control over
USB. This did not establish successful microphone capture, nor prove that this modem implements the loopback
as the manual describes.

The alternate-input repeat sent codec page 1 register 0x34 = 0x20 (IN2L), then enabled local loopback. The
engine reported ready=1, error=0, TX=0 and loopback=1. With the cable removed and knob-controlled volume, the
owner again reported only hiss or silence. Both input checks therefore failed to establish audible microphone
capture. There is still no actual codec readback, so neither physical input wiring nor a failed ADC is proved.
After reconnection the agent disabled monitor/loopback, sent the normal 0x34 = 0x80 input selection, and
confirmed volume.src=knob with override=-1. No RF was requested.

A controlled boot comparison is prepared: omit CODEC_SELECT = 1, which has no established caller in
the stock MCU image. Leave vocoder selection, gains and bypass writes intact. This is a candidate experiment,
not a proven microphone fix. The previous image remains the hardware baseline. No squelch tuning is included.
The comparison artifact is `build/d878uv2-resident-codec/rfoundry_d878uv2_codec.bin`; the unmodified hardware
baseline is preserved at `build/d878uv2-mode-audio-e3acd148/rfoundry_d878uv2_codec.bin`. Codec-profile and shared
SCT tests pass (636 shared checks), as do the normal firmware build, generic check and documentation links.

The owner flashed the comparison; USB version confirms `e16d7eed`. The modem booted successfully and FM
setup completed ready=1, error=0, timeouts=0. Codec register snapshots remain -1. In the normal-input local
loopback on this image, the owner reported a feedback loop after unplugging the cable and raising the knob.
This is consistent with an acoustic microphone path, but no intelligible speech was confirmed and internal
oscillation has not been excluded. Earlier bring-up images also produced squealing, so this observation alone
does not establish an improvement caused by omitting CODEC_SELECT.

On reconnection the agent disabled monitor and loopback, sent the normal IN1L selection, and restored knob
control. Readback showed idle, monitor=0, loopback=0, ready=1, error=0, TX=0 and no transmission since boot.
A brief owner-operated 70 cm FM voice check in both directions is pending. No agent RF was requested.

The owner subsequently confirms FM transmit speech, but it is very quiet in hiss and the peer's volume must
be raised far. On the running `e16d7eed`, mic gain was 5. The agent set debug SETTING `mic_gain=10` in RAM
only; the owner reports speech is "a bit louder." This is not a measured 10 dB increase and does not prove
normal deviation. The question whether the peer hears a strong/quiet, strong/hissy or weak/hissy carrier
while the owner is silent is pending. Gain 10 remains temporary; no setting was saved to NAND.

A separate gain-control defect is corrected in source: the dB offset had also changed AGC target/hysteresis
bits in codec register 0x56. Preserve its baseline 0xA0 while changing the PGA and AGC maximum gain. The
profile host test now checks positive and negative offsets on the wire with the AGC control unchanged.
The candidate artifact is `build/d878uv2-mic-gain/rfoundry_d878uv2_codec.bin`. It has not been flashed and is
not a claim that this small control correction explains all weak-speech or carrier-noise symptoms.

### Weak carrier changes the priority

The peer's carrier is **weak and hissy even while the owner is silent**. Treat RF power, tracking and alignment
as open before pushing microphone gain further. The last 433.500 MHz FM TX snapshot reports frequency word
0x0069D5C0, RF state 0x4046, GPIO 0x9EF2, drive 0x47, MOD_GAIN 0x0DAC and all writes successful. These are
commanded/read RF states, not a measurement of radiated power or carrier frequency.

Source review found a concrete tuning-output bug: `TIMER_BASE(3)` addresses GD32 TIMER3 at 0x40000800,
but `mcu_tim_clock_on(3)` enables GD32 TIMER2, whose PA6 output is at 0x40000400. Use `MCU_TIM(3)` for both
the clock's numbering and the register block. RX entry and return from TX also omitted the tracking update;
both now apply the calibrated receive duty. The host RF fixture was stale and did not compile this path.
It now simulates timer clocks/registers, fails against the old address and missing RX update, and passes the
corrected code, including split-frequency TX/RX restoration.

The candidate is `build/d878uv2-tuning/rfoundry_d878uv2_codec.bin`, including the prior AGC-control correction.
It is not flashed. New leading board view keys `tuning.pwm_ready`, `tuning.pwm_compare` and
`tuning.pwm_expected` expose actual timer enable/compare versus the requested RX calibration. Microphone
gain diagnostics also precede the verbose codec snapshot so reply truncation no longer hides them.
The new HIL check reads these keys without retuning or transmitting. It is syntax-checked only: pytest is
not installed in this shell, and the radio still has the earlier image. Direct SoC register reads on that
image were refused, so no live timer-register or analog-voltage result is claimed.

Validation: the RF startup/regression and codec-profile host tests pass. Normal firmware, CONFIG_DIAG=n,
CONFIG_FM=n (with battery saving off), and CONFIG_DMR=n build without warnings. The diagnostics-off and
FM-off links each hit a transient GCC LTO internal compiler error on the first attempt and passed on retry.
The normal stack check passes, with 16 bytes least free in the idle task. Full CI and the new HIL test were
not run. Only the agent's three temporary tuning configuration trees were removed after validation.

## Resume here

Worktree `/var/home/jan/gitrepos/RadioFoundry/.claude/worktrees/d878uv2`, branch `d878uv2-bringup`, HEAD
`9c077f98`. Everything is committed. **Work in the worktree, not the parent checkout** — the owner is committing
RT-4D work there in parallel and it must not be touched.

Latest image: `build/d878uv2-ramp/rfoundry_d878uv2_codec.bin`. The owner was last running `d878uv2-txsnap`; the
ramp image had not been flashed when the session ended.

## What the radio does today

| Item | State |
| --- | --- |
| Boot, display, all keys, battery, encoder, volume knob | Confirmed working on the radio |
| Flash and restore to stock | Both proven. The owner flashes in update mode (PTT + PF3, red LED blinking) |
| NAND, storage, codeplug write and verify | Confirmed. A fleet codeplug (Betrieb ID 1013) is installed |
| Baseband (SCT3258) boot | Confirmed, `bb ok`. Boot block comes from the owner's own stock file |
| Calibration read | Confirmed. ECC must be off to read the factory block; the radio's tuning points equal the factory defaults |
| DMR receive | Decodes. The "TS1 late" reports stopped on 2 m |
| **DMR transmit, 70 cm** | The call **appears on the other radio**, so sync, LC, colour code and slot all decode — but **no voice is heard** |
| **DMR transmit, 2 m** | Nothing reaches the other radio at all. USB drops while keyed |
| **FM transmit** | Microphone audio absent or far too quiet |

## Fault A — the microphone path is dead in both modes (the main fault)

This is one bug, not two. A 70 cm DMR call keys and is *received* with no voice, and FM transmit audio is
absent. Both modes carry the microphone through the codec into the SCT3258 over I2S — the baseband vocodes DMR
itself and processes analog FM audio the same way — so a dead microphone path explains both symptoms, and the
protocol side is already proven good by the call appearing.

**The next task is the audio bring-up's two missing wire writes.** The spec's §4.6.6 lists 31 field writes done
once at boot, after the baseband boots and immediately before the AT1846S init. Cross-referencing each store id
against the emitter table in §4.6.8 shows most of them emit **nothing** on the wire or are already sent, which
narrows the work sharply. What is missing is exactly two fields:

| Field | Len | Content | Why it matters |
| --- | --- | --- | --- |
| **0x6C** (store id 0x00) | 12 | `00 00 00 3C`, then four big-endian 16-bit limits `0F3C 0A8C 0F3C 0A8C` (3900, 2700, 3900, 2700). The **third and fourth become 0xFFFF on an analog channel** | §4.6.6: "the only place the analog/digital distinction enters the audio chain". Fully specified, no unknowns |
| **0x6B** (store id 0x17) | 10 or 12 | `08 00 08 00`, then calibration +0x0040 bytes +0x0A, +0x0B, +0x0C, +0x0D (factory defaults `80 00 80 00`), then 0, then +0x0E | Per-radio audio values |

Note the length discrepancy on 0x6B: §4.6.6 gives the store write as 0x0A (10) bytes while §4.6.8's emitter row
says field 0x6B is 12 bytes. Resolve before sending.

Already sent correctly, so do not redo: `DEMOD_GAIN` 0x1C, `DIGI_SPK_GAIN` 0x69, `DIGI_MIC_GAIN` 0x68 (all from
calibration), and the six codec registers the emitter maps — page 1 0x10, 0x12, 0x13, 0x34, 0x3B, 0x3C and page 0
0x56, 0x58. Store ids 0x04, 0x05, 0x06, 0x08, 0x09, 0x10, 0x11, 0x18, 0x19, 0x1A, 0x1C, 0x1D, 0x1E and 0x20 emit
**nothing**; 0x12 and 0x07 only when a mode byte is 2 (a Bluetooth path); 0x1B and 0x21 only rewrite a state byte.

Two further things §4.6.8 says that this port does not yet honor:

- **`ANALOG_CONFIG`** (field 0x3C, store 0x1F) is 11 bytes: a mode byte with bit 7 forced set, then ten bytes
  from RAM that are **not available from the image**. It cannot be reproduced faithfully; say so rather than
  inventing ten bytes.
- **The moment is wrong.** §4.7.4 states the FM transmit entry sends *no* baseband field at all: everything goes
  out at the **channel or mode change**, not at PTT. This board calls `d878uv2_modem_analog()` from `tx()`. Moving
  baseband configuration to mode entry is the structurally correct fix.

Also unimplemented from §4.7.4, and cheap: **register 0x58 = 0x8405 on transmit** (receive uses 0x8085 FM /
0x80E5 DMR — the radio reads `0x80E5`, correct for DMR receive), and **register 0x40 = 0x0030** on FM transmit
(bit 0 clear; the radio reads `0x0031`). Steps 4, 5 and 18 also drive PE0 and PA7 **low** at transmit, i.e. the
speaker amplifier off; this board leaves them as the audio path set them.

## Fault B — 2 m transmit

**A spec-side agent was running when this session ended.** Check its result before touching this; it was asked
what selects and enables the VHF transmit path beyond AT1846S gpio5 going low, what gpio1 and gpio3 switch (the
page's open question 9), and whether any band-conditional write is unrecorded. Its output goes to
`/var/home/jan/gitrepos/rf-dirty/docs/spec/hardware-d878uv2.md`.

What the radio proved, read back from the chip **while keyed** on 148.006 MHz in DMR (`reg 1 4`…`13`):

| Reading | Value | Verdict |
| --- | --- | --- |
| frequency word | `0x00242260` | correct: 148006 kHz x 16 |
| register 0x30 | `0x4046` | bit 6 set, bit 5 clear — transmitting |
| register 0x1F | `0x9AF2` | reads back matching what was written; gpio5 low, correct for VHF |
| tuning duty | 1644 | exactly the VHF interpolation of §5.1.2 |
| drive, modulation gain | `0x56`, `0x0DAC` | applied; `0x0DAC` is the factory default §5.3 records for both bands |
| all writes | `1` | nothing in the transmit path failed |

So **every value the board sets for a 2 m transmission is correct**, and the fault is past where the firmware can
see. While keyed on 2 m the USB port drops and re-enumerates **with the MCU running throughout** — RAM survives,
`crash.valid=0`, no watchdog reset. That is a supply dip or RF ingress, not a firmware fault, and it does not
happen on 70 cm. Working hypothesis: RF is generated but not reaching the antenna, i.e. a VHF transmit path that
is not actually selected or enabled.

**Keep 2 m keying short and at low power until this is understood** — a PA that browns out its own supply is the
one failure mode here that can cost hardware.

## Ruled out this session — do not re-chase

- **Colour code and time slot.** Already implemented, in the shared engine: `sct_dmr.c` sends `SCT_F_DMR_CC` from
  the channel and tracks slots. The 70 cm call appearing on the other radio confirms it on the air. The spec
  agent's failure to find them in the stock firmware's *store* does not constrain this port.
- **The PA6 tuning voltage.** Was genuinely missing and is now implemented and confirmed correct on both bands
  (1972 UHF, 1644 VHF, both exactly the spec's arithmetic). It was not the 2 m fault.
- **The modulation gain.** `0x0DAC` on both bands is the radio's real calibrated value, not a fallback.
- **MICBIAS and the codec's routing/mute.** The baseband's boot block owns them, and the block is stock.
- **`VOCODER_SEL` for analog.** `sct_analog.c` already sends 16 (0x10) and the board requests analog on FM
  receive and transmit.
- **The per-radio gains.** Were guessed at 0x14/0x00; now read from calibration (factory defaults 0x0C/0x04/0x08).

## Commands

```sh
cd /var/home/jan/gitrepos/RadioFoundry/.claude/worktrees/d878uv2
make BOARD=d878uv2 BUILD=build/d878uv2-<name>
python3 tools/anytone_update.py --board d878uv2 --port /dev/ttyACM0 build/d878uv2-<name>/rfoundry_d878uv2_codec.bin
make -C tests/host && python3 tools/boards.py check
make BOARD=d878uv2 CONFIG_DIAG=n BUILD=build/t-diag   # then rm -rf build/t-diag
```

Diagnostics over USB, all read-only:

```sh
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 0 0x30      # live AT1846S, readable while keyed
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 1 0         # the board's own decisions, 0..3
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 reg 1 4         # the keyed-transmit snapshot, 4..13
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 crash show; ... watchdog show
```

`reg 0 <r>` is the AT1846S (page 0, read-only, deliberately readable while keyed). `reg 1 <r>` is board state no
register holds: 0 GPIO shadow, 1 tuning duty, 2 drive, 3 modulation gain, then the keyed snapshot 4/5 frequency
word, 6 GPIO, 7 reg 0x30, 8 reg 0x1F, 9 duty, 10 drive, 11 modulation gain, 12 mode, 13 all-writes-ok.
Registers 0x29 and 0x2A **do not read back** the written frequency word — use the snapshot instead.

Only one client can hold the serial port, so nothing can be read while a separate `ptt` command runs; that is why
the snapshot exists.

## Clean room

The implementer never reads vendor firmware, disassembly or any other radio firmware's source. The spec side
works in `/var/home/jan/gitrepos/rf-dirty` with the owner's authorization and writes **prose facts** into its
`docs/spec/hardware-d878uv2.md`; the implementer reads that prose and merges what it needs into this tree's
`docs/spec/hardware-d878uv2.md`. Keep radio-specific facts on the hardware page — `tools/check_generic.py`
enforces that generic docs name no radio.

## Cleanup owed

`build/d878uv2-*` trees are about 1.5 GB each. Remove the superseded ones (`-gains`, `-cal`, `-tune`, `-probe2`,
`-txsnap` once the ramp image is confirmed) and `build/ci` after a CI run. `gpio5` is left high after a
transmission — noted, not fixed, harmless so far.
