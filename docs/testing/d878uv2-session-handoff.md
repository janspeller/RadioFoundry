<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AT-D878UVII PLUS session handoff — 2026-11-16

Supersedes the 2026-11-15 handoff. The bring-up history is in [d878uv2-bringup.md](d878uv2-bringup.md); this file
is only what the next session needs to continue.

## Current hardware checkpoint

Audio-fix follow-up: the shared boot-time codec readback parser rejected reports whose TYPE combines
read bit 0x20 with a protocol selector. It now accepts the read bit with selectors (including 0x23/0x25),
while retaining exact length and field checks. The regression fails before the correction and passes after;
control-only replies remain rejected without changing the output buffer. I2C, codec-profile, analog-engine
and codec-owner tests pass; the default firmware build passes without warnings. This is a diagnostic
correction, not a microphone gain or initialization change. No radio port was present during this check;
no flash, settings change or RF was performed. A new dirty audit is checking remaining reachable stock
microphone settings and startup ordering. Do not label the microphone fixed on these results.

Offline extraction checkpoint: the dirty worker validated the BA container's 1,177 outer checksummed
records and extracted 192 internal load records in six address-preserving flash ranges, totaling 294,472
supplied bytes. Reconstructing the ranges reproduces every byte. The same parser validates three earlier
stock versions. The updater independently constructs these headers from instruction/data section metadata
and an explicit coding selector; the BA payloads use coded variants. Extraction is validated; decoding the
resident instructions and recovering the codec initialization table remain open. Local extraction tools,
manifests and payloads stay in ignored `build/resident-audit/` in the dirty tree. Only prose findings cross
the clean-room boundary. No radio access, flash or RF was performed.

The completed audit labels the six streams as two user applications and vocoders 4, 12, 16 (Analog),
and 23. Vocoders 4/12 are identical across all four supplied versions; analog and application streams
change. Every resident record is coded, the host forwards already coded input, and no validated decoder
or uncoded resident equivalent was recovered. Extraction is complete; codec initialization and mic sample
routing remain unrecovered. The hardware page records the section roles and evidence boundary. No new
firmware image or register change follows from this extraction.

Owner follow-up: the codec-selection image appears to improve audio. This is a subjective improvement,
not confirmation of normal microphone level or intelligible DMR voice. The owner requested continued
offline dirty-worker extraction of the resident baseband image overnight, prioritizing the TI codec
initialization table and FM/DMR microphone routing. Results belong in the dirty specification as prose;
no connected-radio work or additional gain changes are part of that extraction task.

Firmware-only follow-up, November 17: the dirty agent traced the complete normal startup and withdrew its
previous no-caller claim. Stock sends and acknowledges `CODEC_SELECT = 1` before selecting the vocoder.
The board profile now restores that command. A host regression requires its successful ACK before any
vocoder/gain writes and proves a rejected selection stops audio setup. The old image fails this regression.
This is a proven missing startup command, not proof that the microphone is repaired. The owner has neither
scope nor logic analyzer; continue stock-firmware analysis without requiring captures. The dirty agent is
checking remaining microphone initialization. Do not repeat gain changes without evidence.


Previous result: FM on `4dff8084` still produces white noise and quiet speech. All four mic writes and the
saved analog-gain packet are acknowledged, with PGA=ceiling=32 and no errors/faults/timeouts. The new dirty
audit explicitly withdraws any claim that the complete physical microphone path was proven. Codec-bypass
encoding and command builders match; actual capsule/input continuity, bias/ADC state, I2S samples and
modulation-output continuity remain unknown. Further gain/routing changes need new evidence from firmware analysis or real codec readback. See the hardware page's physical-path boundary section.

Current running image: **`4dff8084`**, verified through USB after the owner's manual update from
`build/d878uv2-mic-prepare/rfoundry_d878uv2_codec.bin`. Immediately after boot on DMR simplex 433.450 MHz,
read-only diagnostics show idle, no TX since boot, mic offset=0/requested PGA=32, codec acked=0, error=0,
fault=0, timeouts=0 and rejected=0. Zero acknowledged mic values here mean no batch has run, not a measured
mute. FM config is `-` because analog mode has not run; the requested template is
`804650057809c405dc010e`. An owner-operated FM speech test on normal antennas, 433.500 MHz and L1 has
been requested, with USB reconnected afterward to check acknowledgments. Speech improvement is pending.

The combined image includes
the FM gain fix and complete, acknowledged microphone preparation for both modes. A shared runtime codec
owner serializes volume and mic writes. DMR TX work mode and bursts wait for all four ACKs, and the PA slot
gate requires the new call's completed batch generation. Early PTT release cancels preparation; queued
data with `tx_end` still transmits after preparation. Abort/stop/mode change drain any in-flight ACK.
Timeout or uncertain send fences the codec stream until radio restart. The legacy mobile mute/restore
path remains unchanged. No flash or RF test was performed by the agent; hardware audio is unverified.
After the owner's manual flash, verify the running version, then inspect `board.codec_runtime.*` after
a short owner-operated call. `acked=4`, `error=0`, `fault=0` and equal mic PGA/ceiling verify command acceptance,
not speech capture. Firmware images from earlier checkpoints are retained for comparison.
Validation: shared DMR tests pass 734 checks, including existing mute/restore cases. Codec transaction,
analog profile and RF startup/slot-gate tests pass. Default, DMR-off, FM-off (battery save off) and
diagnostics-off builds pass without warnings, as does the other SCT board's default build. Alternate LTO
links required sequential retries after the known compiler internal error. The default stack check passes
with its 128-byte margin. Both read-only HIL tests collect; they have not run on the unflashed candidate.
Sanitizer checks could not link because the host's ASan/UBSan runtime libraries are missing. Full CI was not run.

New FM gain candidate: `build/d878uv2-fm-gains/rfoundry_d878uv2_codec.bin`. The trace recovered
ANALOG_CONFIG bytes 1–10 from calibration
+0x40..+0x49. The old claim that those bytes were unavailable RAM data was wrong. Both owner calibration
copies match factory words 4650/0578/09C4/05DC/010E. The port now supplies these through a shared analog
configuration template instead of unity gains, with mode bit 7 set and width bits selected by the engine.
This changes TX pre/post-limiter gain and RX gain; it does not change microphone PGA or factory data.
The candidate is not flashed. After the owner updates, verify its running version first, then compare
`board.fm.analog_config` and `board.fm.analog_requested` while receiving FM before an owner-operated speech
test. RX volume may be lower. The register snapshots remain unavailable and the codec-I2C address convention
is unresolved. The trace did not prove a need to reapply line-output gains after vocoder selection.
Codec-profile, analog-engine and factory-calibration host tests pass; the packet regression fails before
the fix. Default, DMR-off, FM-off (battery save off) and diagnostics-off firmware builds pass without warnings.
Two alternate LTO links needed retries after compiler internal errors. The default stack check passes.
The new read-only analog-calibration HIL test collects cleanly but has not run on this unflashed image.
Documentation links and generic-doc checks pass. Full CI was not run. The owner prefers to flash manually.

The subsequent whole-DMR trace is recorded under "Whole DMR and microphone path audit" on the hardware page.
It found a separate digital Mic-setting bug: the board updated PGA and AGC maximum in memory but sent only
PGA at each digital TX; the shared engine also ignores codec acknowledgments. These are not fixed in
`fc1c9900` and do not prove the cause of quiet baseline audio. The newer candidate above applies the coupled,
acknowledged profile and adds rejection, timeout, early-release and legacy-path regression coverage.
No new mandatory DMR ADC/I2S/codec-output initialization was established.
The +40 producer condition was clarified as raw channel type >=2, not ordinary digital type 1. Ordinary FM
type 0 and DMR type 1 keep the saved word. Do not send the recovered analog packet in ordinary DMR based
on that earlier mislabeled condition. The prepared FM binary and its expected version remain unchanged.

Current running image: **`273d7c23`**, verified through USB after the owner's manual update from
`build/d878uv2-fm-mod/rfoundry_d878uv2_codec.bin`. It adds calibrated FM `MOD_GAIN` setup and the
unbuffered-DAC correction. The owner reports a slight overall improvement and somewhat better 70 cm
DMR transmission. The owner clarified that the peer mainly recognizes calls more reliably, without
intelligible voice, while the UV878 still has its 10 W dummy load fitted. This leakage-path test does not
establish voice-link quality. Battery display and button behavior are confirmed normal again. A brief
70 cm DMR voice test with normal antennas, a few meters separation, L1 and programming cable unplugged
has been requested. Do not mark microphone audio fixed or infer its failure from the dummy-load result.

Read-only diagnostics on DMR simplex 433.450 MHz, L2, CC1/TS1, TG99 show idle, TX error=ok, underruns=0,
modem stalls=0 and stuck=0. PWM ready=1, actual=expected=1972; mic offset=0, requested PGA=32.
ADC channel 11 reads 2387 (about 7.69 V by the board conversion). Codec snapshots remain unavailable (-1).
Native modem voice has no MCU microphone sample tap, so zero MCU mic metrics do not establish silence.
No settings were changed and no RF was initiated during these checks. Targeted codec-profile,
analog-engine and RF-startup host tests pass. The profile regression fails before the missing field is
added. Default, DMR-off and FM-off (with battery save off) builds pass without warnings; the FM-off link
needed a retry. Default stack check passes, and documentation links and generic-doc checks pass.
Full CI was not run. SQ5 false openings and VHF transmit behavior remain unresolved.

Before this confirmed update, the owner reported missing button behavior, no TX and 0% battery.
Read-only USB `version` then reported **`31ba6278-dirty`**, not the supplied candidate `273d7c23`. The supplied
file embeds `273d7c23`; its SHA-256 is
`f872cae6f71f78c19448bac511ece046b3d4f55ed46a58d34c710f337b5bc7dd`.
A fresh build of the same source differs only in four bytes encoding the CPS compile-time timestamp.
Those reports must not be attributed to the new gain/DAC changes.
The attempted agent flash stopped at serial-port permission denial and wrote nothing. The owner has been
asked to enter update mode and grant temporary per-device access, then chose to update manually.

## Earlier hardware checkpoints

The owner flashed `8d1b53bc` from `build/d878uv2-tuning/rfoundry_d878uv2_codec.bin`; USB version confirms it.
On FM 433.500 MHz / 12.5 kHz, read-only diagnostics show PWM ready=1, actual compare=1972 and expected=1972.
FM ready=1, error=0, timeouts=0, loopback=0, monitor=0 and state=idle. No TX occurred since that boot at the
checkpoint. Mic gain is back at 5, requested offset 0 dB and PGA count 32. This verifies timer register setup;
physical control voltage, radiated power and carrier alignment are not measured.

The owner is repeating the short 70 cm FM voice/carrier comparison with the programming cable removed,
same power/distance/peer volume as before. The earlier image's carrier was weak and hissy. DMR and VHF
outcomes on the timer-corrected image remain untested. SQ5's intermittent false openings remain open.

The owner subsequently reports no improvement: the weak/hissy symptom remains. The targeted read-only HIL
test `test_tuning_pwm_matches_calibration` now passes on the connected radio, using an isolated environment
at `/tmp/rfoundry-sdr`; do not mistake this register check for a carrier-power or frequency measurement.

A Nooelec NESDR initially failed USB enumeration with descriptor errors -71. Moving it to another USB port
resolved enumeration (0BDA:2838). Receiver tools are installed only in `/tmp/rfoundry-sdr`. The temporary,
receive-only `/tmp/rfoundry_carrier_probe.py` saves numeric spectral summaries, no IQ or audio, and temporarily
detaches/restores only the SDR's DVB interface. Its synthetic frequency/relative-power checks pass. The first
3-second idle capture was unclipped. A subsequent UV878 carrier capture near 433.500 MHz heavily clipped the SDR;
the received peak was around 433.502175 MHz, but the SDR's own frequency error is unresolved.
Do not infer absolute RF power or a calibrated transmitter offset from this capture. The owner offered a
dummy load and is preparing the repeat with a power-rated 50-ohm load on the UV878, leaving the SDR on its
own separate antenna. Wait for readiness and a running capture before asking for the short silent carrier.
No remote PTT was used. The radio was selected to FM 433.500 MHz and effective power L1 for this comparison.

The repeat used a dummy load on each transmitter in turn, nominal FM 433.500 MHz, lowest power, and the
same NESDR settings: center 433.750 MHz, 1.024 MS/s, manual gain 0 dB, digital AGC off. The owner operated
PTT. Numeric summaries are `/tmp/rfoundry-dut-load-carrier.jsonl` and
`/tmp/rfoundry-reference-load-carrier.jsonl`; no IQ or audio was saved. Keeping frames with SNR >20 dB,
clipping fraction <0.0001 and band level between -30 and -3 dBFS gives:

| Transmitter | Accepted frames | Median carrier | Accepted range |
| --- | --- | --- | --- |
| UV878, running `8d1b53bc` | 137 | 433,502,202.2 Hz | 433,502,197.3–433,502,203.7 Hz |
| Reference handheld | 166 | 433,500,032.7 Hz | 433,500,031.5–433,500,035.3 Hz |

The UV878 is **2,169.5 Hz higher relative to the reference**, about 5 ppm. The SDR clock is uncalibrated;
these are relative measurements, not calibrated absolute frequencies. Received levels were comparable,
but dummy-load leakage and coupling do not measure transmitter power. Frequency alignment now needs a
controlled comparison before further microphone gain changes. A temporary VFO offset can test this without
a flash; preserve the existing VFO frequency, mode, tones and channel selection, and restore them afterward.
The temporary VFO comparison is now in progress. Original channel and VFO views are saved in
`/tmp/rfoundry-offset-original-channel.json` and `/tmp/rfoundry-offset-original-vfo.json`. Original selection
was zone 3, position 2, FM simplex 433.500 MHz, effective L2; the VFO was already FM simplex 433.500 MHz,
12.5 kHz, L1, no tones. Only its frequency was changed, to 433,497,812 Hz. Restore the VFO to 433,500,000 Hz
and return to channel mode after the comparison. No flash or saved factory-calibration change was made.

The owner keyed the offset VFO into the dummy load. Under the same receiver settings, 66 accepted frames
in `/tmp/rfoundry-dut-offset-carrier.jsonl` have median carrier 433,499,996.3 Hz: 36.4 Hz below the reference,
versus 2,169.5 Hz above it before. No clipping was observed in those frames. This confirms that the tuning
offset aligns the carrier. The owner repeated the normal-antenna FM voice test and reports **still weak and
hissy**. Carrier alignment alone did not resolve the symptom. Next compare spoken FM deviation against the
reference handheld using the same receive-only setup before changing gains. The temporary VFO offset is
still active; restoration remains owed.
The NESDR interface driver was restored after capture. All PTT operation was manual by the owner.

Spoken-count captures are `/tmp/rfoundry-dut-offset-voice.jsonl` and
`/tmp/rfoundry-reference-voice.jsonl`. The owner confirmed the reference was the DM32 with no cables.
Both captures have little sustained voice-band modulation, so they do not establish a relative microphone
gain deficit. The UV878's stable samples are mostly 70–90 Hz RMS; its large excursions coincide with
keying transitions. Synthetic two-tone FM also passes the measurement check (expected 632 Hz RMS).

A longer DM32 capture, `/tmp/rfoundry-reference-sustained-voice-long.jsonl`, captures the requested silence
followed by sustained "ah." At 42–43 seconds, seven accepted frames have median 10.8 Hz RMS and 30 Hz
99.5th-percentile absolute deviation in the 300–3000 Hz measurement band. At 43–47 seconds, 18 accepted
frames have medians 244.6 Hz RMS and 796.4 Hz percentile deviation. Clipped frames were excluded. These
are descriptive window statistics, not calibrated full-band peak deviation. A matching sustained-sound
UV878 test followed in `/tmp/rfoundry-dut-sustained-voice.jsonl`. The owner reports completion. Two keyed
intervals appear at approximately 7.7–11.8 and 17.7–23.1 seconds. Accepted steady samples mostly remain
70–80 Hz RMS, without the clear sustained rise in the reference test; some edge/handling frames clip.
Do not infer a calibrated gain ratio or identify the failed stage from these summaries. The first 55-second
reference attempt ended without a carrier; allow a longer readiness window. The SDR driver is restored.
The owner has been asked to reconnect USB for restoration and diagnostics. A targeted spec-side audit of
reachable microphone/codec/baseband setup is running in `stock_audio_spec`; no additional gain change or
flash is justified yet by these captures alone.

USB restoration is complete: VFO simplex 433.500 MHz, channel mode zone 3 / position 2 at 433.500 MHz,
effective L2. Live FM ready=1, error=0, timeouts=0, loopback=0, mic offset=0 and requested PGA=32.

The subsequent reachable-stock audit established a concrete omission: FM mode setup sends calibrated
`MOD_GAIN`, but the port supplied that field only to digital TX. The candidate now includes it in analog
setup, updates its band calibration under the modem lock, and restarts analog setup when the gain changes.
This is a verified command omission, not a proven explanation for the audio measurements. The existing
unbuffered-DAC correction is also included; neither change is on the running `8d1b53bc` image yet.

While waiting for the receiver, a separate stock mismatch was corrected in source: both RF-control DAC output
buffers must be disabled before enable, with triggers/waves/DMA off. The saved calibration counts remain
unchanged. The RF host test fails the old channel control fields and passes the new ones; normal firmware
builds and the normal stack check passes. The candidate artifact is
`build/d878uv2-dac-mode/rfoundry_d878uv2_codec.bin`. This DAC-mode candidate is not flashed and needs an RF comparison. The running baseline remains
`8d1b53bc`. Stock RF init and conditional stored `1846N` metadata findings are on the hardware page; their
register differences are not applied.

## Continuation: missing mode controls implemented

The current candidate is `build/d878uv2-mode-audio/rfoundry_d878uv2_codec.bin`. It adds mode-entry 0x6C and
12-byte 0x6B, using calibration +0x4A..+0x4E for known bytes and zero reserved trailing bytes. The ten-byte
startup store update is not the twelve-byte wire length. Digital setup uses the shared engine's existing
control-field classification; analog setup now accepts acknowledged board control fields. Per-call signaling
is retained. At that checkpoint ANALOG_CONFIG still used nominal gains; the later trace above recovers its source.

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

Historical comparison, now superseded by the November 17 reachability correction: omit CODEC_SELECT = 1,
which the earlier incomplete audit reported had no established caller in
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
It is now flashed; see the hardware checkpoint above. New leading board view keys `tuning.pwm_ready`, `tuning.pwm_compare` and
`tuning.pwm_expected` expose actual timer enable/compare versus the requested RX calibration. Microphone
gain diagnostics also precede the verbose codec snapshot so reply truncation no longer hides them.
The new HIL check reads these keys without retuning or transmitting. It is syntax-checked only: pytest is
not installed in this shell. The checkpoint above manually verifies the same register comparison on the new
image without running pytest. Direct SoC register reads on the earlier
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

- **`ANALOG_CONFIG`** (field 0x3C, store 0x1F) is 11 bytes. The original handoff incorrectly called its ten
  RAM gain bytes unrecoverable. The later trace follows their producer to calibration +0x40..+0x49; the
  current candidate now uses them unchanged for pure FM, with mode bit 7 set.
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
