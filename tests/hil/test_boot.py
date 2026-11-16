# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""The radio is up and the debug link works: version and features, screenshot geometry, no stalls, error replies."""
import struct
import time

import pytest

from rfoundry_hil import DebugLinkError, as_int, skip


@pytest.mark.feature("debuglink")
def test_version_and_features(radio):
    v = radio.version(fresh=True)
    assert v.get("proto") == "1"
    assert v.get("board"), "board name"
    assert v.get("git") and v["git"] != "unknown", "firmware built from a git tree"
    feats = radio.features()
    assert {"HOSTLINK", "DEBUGLINK"} <= feats, feats    # USB too on radios that have it (not the DM-32)
    assert "FM" in feats or "DMR" in feats


@pytest.mark.feature("debuglink")
def test_ping_latency(radio):
    times = [radio.ping() for _ in range(20)]
    assert max(times) < 0.15, f"slowest ping {max(times) * 1000:.0f} ms"


@pytest.mark.feature("debuglink")
def test_screenshot_geometry(radio, shots):
    w, h, bpp = (int(x) for x in radio.version()["display"].split("x"))
    if bpp == 4:
        bpp = 16        # a 16-color panel is sent as RGB565 (debuglink.md 3.3)
    info, data, stable = radio.screenshot()
    assert (info["width"], info["height"], info["bpp"]) == (w, h, bpp)
    stride = w * 2 if bpp == 16 else (w + 7) // 8
    assert len(data) == info["total"] == stride * h
    assert stable, "the screen kept changing during the capture"
    assert info.get("packed"), "the firmware sent only the RGB565 form, not the packed one (debuglink.md 3.3)"
    radio.save_screenshot(f"{shots}/boot.png")


@pytest.mark.feature("debuglink")
def test_no_stalls(radio):
    """The firmware clock advances with wall time, the link answers throughout, nothing timed out."""
    link0 = radio.view(["link"])
    t0, m0 = time.monotonic(), as_int(link0["uptime_ms"])
    for _ in range(10):
        radio.ping()
        time.sleep(0.2)
    link1 = radio.view(["link"])
    dt_fw = (as_int(link1["uptime_ms"]) - m0) / 1000
    dt = time.monotonic() - t0
    assert abs(dt_fw - dt) < 0.3, f"firmware clock {dt_fw:.2f} s vs wall {dt:.2f} s"
    assert link1["dl.timeouts"] == link0["dl.timeouts"], "partial frames timed out"
    assert link1["dl.bad_crc"] == link0["dl.bad_crc"]


@pytest.mark.feature("debuglink")
def test_view_sections(radio):
    v = radio.view()
    for k in ("view", "state", "rx_hz", "ch.name", "dmr.built", "hostlink", "ui.screen", "input.idle_ms"):
        assert k in v, k
    # View 5: the CPS counters moved from the storage section to cps (debuglink.md 3.7)
    assert v["view"] == "5"


@pytest.mark.feature("debuglink")
def test_diag_has_clock_and_lines(radio):
    ms0, lines = radio.link.diag()
    assert len(lines) >= 2 and lines[0].startswith("radio ")
    time.sleep(0.5)
    ms1, _ = radio.link.diag()
    assert 300 < ms1 - ms0 < 1500


@pytest.mark.feature("debuglink")
def test_reset_counters_returns_clock(radio):
    t = radio.reset_counters()
    assert abs(t - as_int(radio.view(["link"])["uptime_ms"])) < 1000


@pytest.mark.feature("debuglink")
def test_log_readable(radio):
    dropped, text = radio.log()
    assert dropped >= 0
    assert all(line.split(" ", 1)[0].isdigit() for line in text.splitlines()), text[:200]


@pytest.mark.feature("debuglink")
def test_unknown_command_and_bad_crc(radio):
    with pytest.raises(DebugLinkError) as e:
        radio.link.request(0x7E)
    assert e.value.status == 1
    # A frame with a broken CRC gets an error frame (command 0xFF, status 5)
    from rfoundry_debug import frame
    f = bytearray(frame(0x01, 0x33, b"x"))
    f[-1] ^= 0xFF
    radio.link.s.write(bytes(f))
    r = radio.link._read_frame(time.monotonic() + 1.0)
    assert r is not None and r[0] == 0xFF and r[1] == 0x33 and r[2][0] == 5, r
    radio.ping()   # still fine afterwards


@pytest.mark.feature("debuglink")
def test_oversized_request_rejected(radio):
    hdr = bytes([0xA5, 0x5A, 0x01, 0x44]) + struct.pack("<H", 65)
    radio.link.s.write(hdr)
    r = radio.link._read_frame(time.monotonic() + 1.0)
    assert r is not None and r[0] == 0xFF and r[2][0] == 6, r
    radio.ping()


@pytest.mark.feature("gps")
@pytest.mark.reboot
def test_gps_startup_survives_restart(radio):
    """GPS power through an RF chip must not fault before or during normal startup."""
    if "GPS" not in radio.features():
        skip("not_built", "built without GPS")
    radio.link.reboot(0, radio.link.chip()["token"])
    radio.link.close()
    time.sleep(2.0)
    radio.reconnect(30.0, "GPS startup")
    previous = as_int(radio.view(["link"])["uptime_ms"])
    # Cover the initial module probe and the missing-module shutdown path.
    deadline = time.monotonic() + 12.0
    while time.monotonic() < deadline:
        time.sleep(0.5)
        current = as_int(radio.view(["link"])["uptime_ms"])
        assert current >= previous, "radio restarted during GPS startup"
        previous = current


@pytest.mark.feature("debuglink")
def test_codec_boot_snapshot(radio):
    """Boards with codec readback expose actual startup register values, not write shadows."""
    values = {k: as_int(v) for k, v in radio.view(["board"]).items() if k.startswith("board.codec.")}
    if not values:
        skip("no_hw", "board does not expose a codec boot snapshot")
    assert all(0 <= v <= 255 for v in values.values()), f"codec boot readback failed: {values}"


@pytest.mark.feature("dmr_voice")
@pytest.mark.reboot
def test_dmr_channel_starts_after_boot(radio):
    """A saved DMR channel must become active when asynchronous baseband boot completes."""
    if radio.view(["core"]).get("mode") != "dmr":
        skip("state", "select a DMR channel before this restart test")
    radio.wait_for(lambda v: v.get("settings.dirty") == "0", timeout=10.0, sections=("storage",),
                   what="settings saved before restart")
    radio.link.reboot(0, radio.link.chip()["token"])
    radio.link.close()
    time.sleep(2.0)
    radio.reconnect(30.0, "DMR startup")
    assert radio.view(["core"]).get("mode") == "dmr", "saved DMR channel was not restored"
    radio.wait_for(lambda v: v.get("dmr.active") == "1", timeout=10.0, sections=("dmr",),
                   what="DMR modem active without switching channels")


@pytest.mark.feature("fm")
def test_analog_baseband_ready(radio):
    """An FM channel with a baseband audio path must finish its analog-call setup."""
    if radio.view(["core"]).get("mode") != "fm":
        skip("state", "select an FM channel for the analog-baseband check")
    before = radio.view(["board"])
    if "board.fm.enabled" not in before:
        skip("no_hw", "board does not expose analog-baseband state")
    state = radio.wait_for(lambda v: v.get("board.fm.ready") == "1" or v.get("board.fm.error") != "0",
                           timeout=5.0, sections=("board",), what="FM baseband setup")
    assert state.get("board.fm.enabled") == "1" and state.get("board.fm.error") == "0", state
    assert state.get("board.fm.ready") == "1", state


@pytest.mark.feature("debuglink")
@pytest.mark.manual
def test_analog_codec_loopback(radio):
    """Opt-in codec loopback completes and restores RX without requesting RF TX."""
    before = radio.view(["board", "core"])
    if "board.fm.loopback" not in before:
        skip("no_hw", "board does not expose codec loopback")
    if before.get("mode") != "fm" or before.get("state") != "idle":
        skip("state", "select an idle FM channel with scan and dual watch off")
    if before.get("board.fm.ready") != "1" or before.get("board.fm.loopback") != "0":
        skip("state", "normal analog receive must be ready")
    try:
        radio.link.chip_reg(2, 0x18, 0x11)
        radio.wait_for(lambda v: v.get("board.fm.ready") == "1", timeout=3.0,
                       sections=("board",), what="codec loopback command complete")
        state = radio.view(["board", "core"])
        assert state["board.fm.loopback"] == "1", state
        assert state["board.fm.error"] == "0" and state["state"] == "idle", state
    finally:
        radio.link.chip_reg(2, 0x18, 0)
    radio.wait_for(lambda v: v.get("board.fm.ready") == "1" and v.get("board.fm.loopback") == "0",
                   timeout=3.0, sections=("board",), what="normal analog RX restored")


@pytest.mark.feature("debuglink")
def test_factory_control_dac_calibration(radio):
    """A board with the factory DC-control path must select a valid saved 12-bit count."""
    view = radio.view(["board"])
    if "board.control_dac.count" not in view:
        skip("no_hw", "board does not expose factory DC-control calibration")
    assert 0 <= as_int(view["board.control_dac.count"]) <= 4095, view
    if "board.control_dac.map_entries" in view:
        assert 0 <= as_int(view["board.control_dac.map_entries"]) <= 89, "factory block map unavailable"
    assert view["board.control_dac.factory"] == "1", "factory word unavailable; control DAC must remain uninitialized"


@pytest.mark.feature("debuglink")
def test_factory_calibration_read_windows(radio):
    """The diagnostic exposes only small, mapped, read-only calibration record windows."""
    state = radio.view(["board", "core"])
    if state.get("board.cal.read_windows") != "1":
        skip("no_hw", "board does not expose calibration read windows")
    if state.get("board.control_dac.map_status") != "4" or state.get("state") != "idle":
        skip("state", "mapped calibration reads need a loaded map and an idle radio")
    for page in (0, 1):
        raw = radio.link.chip_reg_bytes(3, 0, 3, page=page + 4)
        assert len(raw) == 3 and raw[0] in (0, 1)
        if raw[0] == 0:
            assert radio.link.chip_reg_bytes(3, 0, 2, page=page) == raw[1:]
        audio = radio.link.chip_reg_bytes(3, 0x80, 32, page=page + 4)
        assert len(audio) == 32 and audio[0] in (0, 1)
    for page in (2, 3):
        raw = radio.link.chip_reg_bytes(3, 0, 9, page=page + 4)
        assert len(raw) == 9 and raw[0] in (0, 1)
        if raw[0] == 0:
            assert radio.link.chip_reg_bytes(3, 0, 8, page=page) == raw[1:]
