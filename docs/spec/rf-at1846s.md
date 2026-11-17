<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# AT1846S register profiles

The shared driver accepts optional board data for chip revisions whose initialization, bandwidth or mode
registers differ. The default tables remain active when no replacement is supplied. Board wiring and
calibration choose the profile; the driver does not test a radio identifier. Supported radios are listed
in the [feature matrix](../feature-matrix.md).

`at1846s_set_profile` installs a static-lifetime profile and invalidates the applied bandwidth and mode.
Call it again after mutable bandwidth values change. Each instance retains its own register cache.

## Initialization

An initialization step contains a register page, register, value and delay after the write. Register
`AT1846S_STEP_WAIT` represents a delay without a write. Initialization forces writes in order; a soft
reset of register 0x30 invalidates the register cache. Bus failures make initialization fail.

An optional bounded probe reads chip identity 0x1846 from register zero. It stops at a match or after
`probe_tries`; mismatches and read failures delay `probe_delay_ms` before retry. Exhaustion does not
abort the register sequence. Profiles without a probe skip this stage.

After the supplied initialization steps, the driver applies the narrow bandwidth profile and ends idle.
A missing initialization list uses the existing default reset/calibration sequence.

## Bandwidth and mode

A bandwidth row provides narrow and wide values and a width mask: bit 0 includes narrow, bit 1 includes
wide. Rows apply in order with cached writes. The driver returns to page zero, writes the board's idle
control word and invalidates the applied mode. A missing bandwidth list uses the default table.

Each mode can provide its own ordered register steps. A missing mode list uses the default table for
that mode. A mode outside the supported FM/DMR enumeration fails without bus writes. The board may
then apply its receive/transmit trims; calibrated final deviation belongs after tone selection.

## Verification

Host tests cover the default sequences, profile selection, delays, width masks, page restoration,
mode fallback and cache invalidation. Boards supplying a profile test their factory selection and
frequency-dependent values. Passive hardware checks may compare the advertised profile's registers
while receiving. Those checks must not key or retune the radio.
