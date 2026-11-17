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


@pytest.mark.feature("fm")
def test_rf_profile_registers(radio):
    view = radio.view(["board", "core", "channel"])
    if view.get("board.rf.profile") != "at1846n":
        skip("no_hw", "board does not report this RF profile")
    if view.get("mode") != "fm" or view.get("state") not in ("idle", "rx"):
        skip("state", "RF profile comparison requires FM receive mode")
    expected = {0x54: 0x2A18, 0x5A: 0x0CDD, 0x63: 0x3FFF, 0x3C: 0x0408}
    width = view.get("ch.bw")
    if width not in ("12k5", "25k"):
        skip("state", "channel bandwidth is unavailable")
    expected[0x60] = 0x1CB8 if width == "12k5" else 0x203E
    expected[0x62] = 0x0A10 if width == "12k5" else 0x2346
    for reg, value in expected.items():
        actual = radio.link.chip_reg(0, reg)
        assert actual == value, f"RF profile register {reg:02X}: {actual:04X}, expected {value:04X}"


@pytest.mark.feature("dmr")
def test_digital_rf_profile_registers(radio):
    view = radio.view(["board", "core", "channel"])
    if view.get("board.rf.profile") != "at1846n":
        skip("no_hw", "board does not report this RF profile")
    if view.get("mode") != "dmr" or view.get("state") not in ("idle", "rx"):
        skip("state", "RF profile comparison requires DMR receive mode")
    high = int(view.get("rx_hz", "0")) >= 300000000
    expected = {0x0A: 0x4381, 0x33: 0x44D5 if high else 0x44DD,
                0x34: 0x2398 if high else 0x2392, 0x3C: 0x1A32, 0x40: 0x0031,
                0x41: 0x4006, 0x48: 0x192F, 0x50: 0x010B, 0x56: 0x0B02,
                0x58: 0x80E5, 0x60: 0x101E, 0x62: 0x0A10}
    for reg, value in expected.items():
        actual = radio.link.chip_reg(0, reg)
        assert actual == value, f"RF profile register {reg:02X}: {actual:04X}, expected {value:04X}"
