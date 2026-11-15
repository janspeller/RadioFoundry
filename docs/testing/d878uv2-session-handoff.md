<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AT-D878UVII PLUS session handoff — 2026-11-15

## Resume here

Worktree: `/var/home/jan/gitrepos/RadioFoundry/.claude/worktrees/d878uv2`.
Branch: `d878uv2-bringup`, HEAD `31ba6278`. All changes from this session are **uncommitted**, including new
host test sources and stub headers. Continue in this worktree, not the parent checkout. The parent checkout is
on another branch and had unrelated codeplug edits; those were left alone.

The owner is currently running `build/d878uv2-controls/rfoundry_d878uv2_codec.bin`.

| Item | Latest hardware evidence |
| --- | --- |
| Application startup | Owner confirms normal boot after GPS ordering fix |
| LCD and backlight | Early probe drew the splash; original picture was 180 degrees rotated. MADCTL changed from 0x68 to 0xA8. Explicit confirmation of the corrected orientation was not recorded |
| Keypad | Owner now confirms **all keys work** after the controls image |
| Battery | Owner now confirms the battery bar works; absolute voltage/percentage accuracy is not calibrated |
| Channel encoder | Owner confirms it works, but **two physical clicks are needed per channel change** |
| Volume knob/bar | ADC initialization fixed in code; owner has not explicitly confirmed the bar or audio level yet |
| Mechanical off click | Owner confirms the control is a volume potentiometer with a mechanical on/off click. Shutdown after the latest image still needs confirmation |
| USB/debug link | Read successfully on the normal GPS-fix image at `/dev/ttyACM0`; not checked again after the controls image |
| RF, GPS fix, audio | Not validated in this session. No RF transmission was commanded by the agent |

The next task is the encoder's two-click ratio. Current code uses `quad_init(&knob, 4, QUAD_REST_11, knob_ab())`.
The observation suggests two transitions per physical detent. Check raw transitions and rest states over ten
clicks; likely correction is two steps with `QUAD_REST_EQUAL`. **That correction has not been made or built.**
Keep the persistent consumed count and interrupts on both encoder lines.

## Root causes and implemented fixes

1. **Installer refusal, preceding this session.** The model mapping existed but the board had no variant claiming
   the image. The existing worktree now declares variant `GD`, `GD32F303VGT6`, running `d878uv2`, and its registry
   is regenerated. The owner had already verified stock restoration. No installer safety check was weakened.
2. **Black screen/reboot loop.** GPS was created in `board_init`, before RF initialization. Its power callback
   writes an AT1846S GPIO and could dereference an uninitialized bus while startup yielded in display delays.
   Crash probe displayed `fault 3 at 20001E50 in gps`, LR `08032739`, CFSR `00020000`, HFSR `40000000`.
   LR in the crash-probe ELF maps to the indirect bus call in `at1846s_write_force`. GPS task creation now runs
   at the end of the RF driver's board-level `init`, after bus attachment and chip initialization. Normal boot
   was then confirmed by the owner.
3. **Upside-down splash.** `display_d878uv2.c` changes `madctl_xor` from 0xC8 to 0x08, selecting MADCTL 0xA8.
4. **Battery 0 and no volume movement.** Nothing called `d878uv2_power.init()`, so the shared ADC driver remained
   uninitialized and returned zero. `board_init` now calls the power interface before other peripheral work.
5. **Encoder absent/replayed counts.** The code passed the two-bit pin value as a rest-state bit mask, then reset
   the consumed count on every `encoder()` call. It now uses a named rest mask and persistent `knob_taken`.
   Both PC8 and PC9 are sampled on both edges through EXTI8/9, rather than the 10 ms key scan. The four-step
   setting is still wrong for the owner's physical click ratio, as noted above.
6. **Only numeric keys 7/8/9 responded.** Matrix settling increased from a short guessed busy loop to 5 us.
   Each scan now restores the previous LCD data output bits, so preempting a display write does not change its
   byte. The keypad map was not changed. All keys now work; the session did not isolate which of those changes
   resolved the symptom.
7. **Knob would not power off.** Diagnostic builds deliberately extended the PA0 shutdown guard to about 49 days.
   The controls image uses the normal 8-second guard again. PA0 polarity and actual off-click behavior still
   need confirmation. The present power-key debounce requires a released state before a 2-second off request;
   do not assume the mechanical switch is a push button.

## Images and commands

Latest image, with normal application startup and normal shutdown guard:

```sh
cd /var/home/jan/gitrepos/RadioFoundry/.claude/worktrees/d878uv2
make BOARD=d878uv2 BUILD=build/d878uv2-controls
python3 tools/anytone_update.py --board d878uv2 --port /dev/ttyACM0 build/d878uv2-controls/rfoundry_d878uv2_codec.bin
```

The owner flashes it in update mode: PTT + PF3, red LED blinking. Normal startup follows a power cycle.
Installer dry run: 1,030,144 bytes to 0x08004000, 32,192 frames, modem boot block included. Ignored codec/stock
files stay local and must not be committed. Stock restoration uses the existing `--restore` command with the
owner's vendor update set; it was not run by this agent.

Other local image directories: `build/d878uv2-power-diag`, `build/d878uv2-startup-probe`,
`build/d878uv2-startup-stages`, `build/d878uv2-crash-probe`, and `build/d878uv2-gps-fix`.
These are diagnostic or superseded images, **not** the latest controls image. Source changes after each build
mean the matching ELF must be retained to interpret its fault addresses; commit text alone is insufficient.

`D878UV2_STARTUP_PROBE=1` stops at an early splash with a blinking green LED. Probe 2 continues through startup
with stage pulses; seven means board initialization returned. Probe 3 displays a valid retained crash record
before starting tasks; otherwise it continues like probe 2. These opt-in hooks remain in `board.c`, disabled in
normal builds. Probe stage 6 was removed when GPS initialization moved to RF initialization.

## Diagnostics and remaining checks

```sh
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 diag
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 view --section board
python3 tools/rfoundry_debug.py --port /dev/ttyACM0 view --section link
```

The controls image adds `board.adc.10`, `.11` (battery), `.12` (volume), `board.power_switch` (raw PA0),
`board.knob_ab` (PC8 bit 0, PC9 bit 1), `board.matrix_idle`, and `board.matrix.0` through `.7`.
The link view already exposes encoder transitions, detents, illegal transitions, steps and consumed total.
Direct SoC register requests were refused by the firmware; no bypass was attempted.

Confirm the encoder with the existing manual `tests/hil/test_encoder.py` test. Confirm volume travel/bar and
shutdown separately. For shutdown, wait at least 8 seconds after boot, turn past the off click, then allow
2 seconds. If it stays on, observe raw PA0 while switching rather than guessing its polarity.

## Validation and files to preserve

- Full host suite passed after the controls changes, including local socket tests outside the sandbox.
- New `boardPowerStartupTest`, `matrixEncoderTest`, and `rfGpsStartupTest` pass. Removing power initialization,
  restoring the invalid rest mask, or resetting the consumed count each read reproduces test failures.
- Controls firmware and `CONFIG_DIAG=n` builds passed without compiler warnings. `CONFIG_GPS=n` was checked
  earlier for the GPS-ordering change, before the controls changes. Temporary `build/t-*` trees were removed.
- Installer dry run, document links, generic-document check and `git diff --check` passed.
- New opt-in HIL GPS startup/restart test in `tests/hil/test_boot.py` is syntax-checked, not run on hardware.
- No full `tools/ci.sh` run, commits, pushes or radio writes by this agent.

Preserve the modified board sources, documentation, `tests/hil/test_boot.py`, `tests/host/Makefile`, the three
new host test C files, and new headers in `tests/host/stubs/input_mcu/` and `tests/host/stubs/rf_startup/`.
Build products and vendor files remain ignored. See [bring-up history](d878uv2-bringup.md) and the
[hardware spec](../spec/hardware-d878uv2.md) for details.
