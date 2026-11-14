# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Frequency detect (docs/spec/freq-detect.md): Tools > Freq detect starts a search (core view fdet=1), Back stops it and
the channel is tuned again. Receive only: the radio never keys. Opt-in: with a second radio held next to this one and
keyed by hand on a known frequency ($RFOUNDRY_HIL_FD_HZ, Hz; a frequency you are licensed for), the reading is within
one VFO step of it (the search runs in the range that holds the channel's receive frequency, so pick a
channel in the same band). The host tests cover the refusals and the state machine."""
import os

import pytest

from rfoundry_hil import skip


def open_detect(radio):
    if "FREQ_DETECT" not in radio.features():
        skip("not_built", "built without CONFIG_FREQ_DETECT (no frequency scan on this radio's RF chip, or switched off)")
    radio.go_home()
    if radio.screen() == "hotspot":
        skip("state", "the radio is in hotspot mode")
    if radio.core().get("mode") != "fm":
        skip("state", "the channel in use isn't FM: the search is FM only")
    # Hub > Tools; Freq detect is Tools' last row, one Up from the first
    for k in ("ok", "6", "up", "ok"):
        radio.press(k)
    if radio.screen() != "freq-detect":
        skip("state", f"Tools' last row didn't open the search (screen {radio.screen()})")
    return radio.wait_for(lambda v: v.get("fdet") is not None, what="the fdet key")


@pytest.mark.feature("freq_detect")
def test_search_starts_and_stops(radio):
    v = open_detect(radio)
    assert v.get("fdet") == "1", f"the search didn't start (refused {v.get('fdet.refused')})"
    assert v.get("fdet.refused") == "none", v
    radio.press("back")
    radio.wait_for(lambda v: v.get("fdet") == "0", what="the search stopped")
    assert radio.screen() == "tools"
    radio.go_home()


@pytest.mark.feature("freq_detect")
def test_reading_matches_a_carrier(radio):
    hz = os.environ.get("RFOUNDRY_HIL_FD_HZ")
    if not hz:
        skip("opt_in", "set RFOUNDRY_HIL_FD_HZ to the frequency a second radio next to this one is keyed on")
    hz = int(hz)
    v = open_detect(radio)
    assert v.get("fdet") == "1", f"the search didn't start (refused {v.get('fdet.refused')})"
    print(f"key the second radio on {hz / 1e6:.4f} MHz now")
    try:
        v = radio.wait_for(lambda v: int(v.get("fdet.hits", "0")) > 0, timeout=30.0, what="a reading")
        step = int(radio.core().get("vfo_step_hz", "12500") or 12500)
        got = int(v.get("fdet.hz"))
        assert abs(got - hz) <= step, f"read {got} Hz, the carrier is on {hz} Hz"
    finally:
        radio.press("back")
        radio.go_home()
