# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Compare acknowledged analog setup with the board's calibration profile; never key or retune."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("fm")
def test_analog_config_matches_calibration(radio):
    view = radio.view(["board", "core", "channel"])
    if "board.fm.analog_config" not in view:
        skip("no_hw", "board has no acknowledged analog configuration diagnostics")
    if view.get("mode") != "fm" or view.get("state") not in ("idle", "rx"):
        skip("state", "analog calibration comparison requires FM receive mode")
    if view["board.fm.analog_config"] == "-":
        skip("state", "analog configuration has not been acknowledged")
    actual = bytes.fromhex(view["board.fm.analog_config"])
    requested = bytes.fromhex(view["board.fm.analog_requested"])
    assert len(actual) == len(requested) == 11
    assert actual[1:] == requested[1:], "acknowledged analog gains differ from the board calibration"
    assert actual[0] & ~0x0A == requested[0] & ~0x0A, "channel width erased other analog flags"
    width = view.get("ch.bw")
    if width not in ("12k5", "25k"):
        skip("state", "channel bandwidth is unavailable")
    assert actual[0] & 0x0A == (0x0A if width == "25k" else 0), "analog channel filters have the wrong width"
