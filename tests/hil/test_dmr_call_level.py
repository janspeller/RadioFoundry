# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Call-level DMR (docs/spec/dmr-call-level.md): a DMR chip that runs calls itself, driven over its UART.

On a DMR channel the chip powers up and reports ready (dmrc.ready=1, its driver's dmrc.state=ready), and it stays
ready. Receive only: the radio under test never keys in these tests except test_ptt, which is radio_tx and needs
--allow-rf. The second radio (a DMR radio in direct mode on the same frequency, color code and slot) is keyed by hand
in the opt-in tests:

  RFOUNDRY_HIL_DMR_PEER=1   test_peer_call: key the second radio when the test says so. The call heard shows in the
                            call view (rx_voice, its IDs) with a signal level (call.rssi_dbm, never the -160 dBm of
                            nothing heard) and ends with the chip's end event; the chip's raw event (dmrc.heard_raw),
                            its level (dmrc.rssi_raw) and its other reports are printed, their formats being partly
                            open
  RFOUNDRY_HIL_DMR_PEER_ID  the second radio's own DMR ID: test_peer_call then checks the caller's ID read (whether the
                            chip sends IDs as BCD is open)
  test_key_loaded           the channel's key (the radio's key store) reaches the chip; skips when the radio has none
  test_ptt                  a call from this radio (--allow-rf): watch the second radio show it

The host tests (tests/host/hf6853Test.c, dmrCallLevelTest.c) cover the protocol and the core against fakes."""
import os
import time

import pytest

from rfoundry_hil import skip


def dmr_channel(radio):
    if "DMR_CALL" not in radio.features():
        skip("not_built", "built without CONFIG_DMR_CALL (no call-level DMR chip on this radio)")
    radio.go_home()
    try:
        radio.find_channel(None, "dmr", simplex=True)
    except AssertionError:
        skip("state", "no simplex DMR channel in the codeplug")
    return radio.wait_for(lambda v: v.get("dmrc.ready") == "1", timeout=15.0, sections=("dmr",),
                          what="the chip's ready report")


def driver(radio):
    """The chip driver's own keys: dmrc.* in dmr_stats keeps the first of them, the board section has them all as
    board.dmrc.*"""
    d = {k: v for k, v in radio.view(["dmr_stats"]).items() if k.startswith("dmrc.")}
    d.update({k[len("board."):]: v for k, v in radio.view(["board"]).items() if k.startswith("board.dmrc.")})
    return d


@pytest.mark.feature("dmr_call")
def test_chip_ready(radio):
    v = dmr_channel(radio)
    assert v.get("dmr.active") == "1", v
    assert v.get("dmrc.fault") == "0", v
    d = driver(radio)
    print("driver:", d)
    assert d.get("dmrc.state") == "ready", d
    assert int(d.get("dmrc.frames_rx", "0")) > 0, d
    # Still ready a while later: no lost link, no power cycle
    faults = int(v.get("dmrc.faults", "0"))
    time.sleep(5.0)
    v = radio.view(["dmr"])
    assert v.get("dmrc.ready") == "1" and int(v.get("dmrc.faults", "0")) == faults, v
    radio.go_home()


@pytest.mark.feature("dmr_call")
def test_back_to_fm(radio):
    dmr_channel(radio)
    try:
        radio.find_channel(None, "fm")
    except AssertionError:
        skip("state", "no FM channel in the codeplug")
    v = radio.wait_for(lambda v: v.get("dmrc.running") == "0", timeout=5.0, sections=("dmr",),
                       what="the chip powered off")
    assert driver(radio).get("dmrc.state") == "off"
    # The RF chip is the MCU's again: its status reads (a level, not the -160 dBm of a bus held by the DMR chip)
    core = radio.wait_for(lambda c: int(c.get("rssi_dbm", "-160")) > -160, timeout=3.0, what="an RSSI reading")
    print("fm:", core.get("rssi_dbm"), v.get("dmrc.faults"))
    radio.go_home()


@pytest.mark.feature("dmr_call")
def test_peer_call(radio):
    if not os.environ.get("RFOUNDRY_HIL_DMR_PEER"):
        skip("opt_in", "set RFOUNDRY_HIL_DMR_PEER=1 and have a second DMR radio in direct mode on the channel")
    dmr_channel(radio)
    before = driver(radio)
    print("key the second radio for about 5 s now (a group call on the channel's talkgroup); listen for it here")
    v = radio.wait_for(lambda v: v.get("dmr.phase") == "rx_voice", timeout=30.0, sections=("dmr",),
                       what="a call heard (the chip's 0x60 event)")
    during = radio.view(["dmr_call"])
    print("heard:", {k: during.get(k) for k in ("call.src", "call.dst", "call.group", "call.to_us")})
    print("raw:", driver(radio).get("dmrc.heard_raw"))
    assert int(during.get("call.src", "0")) != 0, during
    # The chip's signal level reaches the call view while the call plays (asked for every 500 ms)
    level = radio.wait_for(lambda c: int(c.get("call.rssi_dbm", "0")) not in (0, -160), timeout=3.0,
                           sections=("dmr_call",), what="a signal level for the call heard")
    d = driver(radio)
    print("level:", level.get("call.rssi_dbm"), {k: d.get(k) for k in ("dmrc.carrier", "dmrc.rssi_raw")})
    # The call ends with the chip's end event once the second radio stops (the driver's fallback is 30 s)
    radio.wait_for(lambda v: v.get("dmr.phase") == "idle", timeout=60.0, sections=("dmr",), what="the call's end")
    after = driver(radio)
    last = radio.view(["dmr_call"])
    print("before:", before)
    print("after: ", after)
    print("last:", {k: last.get(k) for k in ("last.src", "last.dst", "last.group", "last.ms")})
    assert int(last.get("last.src", "0")) == int(during["call.src"]), last
    assert int(after.get("dmrc.heard_events", "0")) > int(before.get("dmrc.heard_events", "0")), after
    if int(after.get("dmrc.heard_quiet", "0")) > int(before.get("dmrc.heard_quiet", "0")):
        print("note: the call ended by the fallback time-out, not the chip's end event")
    peer = os.environ.get("RFOUNDRY_HIL_DMR_PEER_ID")
    if peer:
        assert int(during["call.src"]) == int(peer), (during["call.src"], after.get("dmrc.heard_not_bcd"))
    v = radio.view(["dmr"])
    assert v.get("dmrc.ready") == "1", v
    radio.go_home()


@pytest.mark.feature("dmr_aes")
def test_key_loaded(radio):
    if "DMR_AES" not in radio.features():
        skip("not_built", "built without CONFIG_DMR_AES")
    v = dmr_channel(radio)
    if v.get("dmr.key_id", "0") == "0":
        skip("state", "no key in use: the key store holds none, or privacy is off")
    v = radio.wait_for(lambda v: v.get("dmr.key_loaded") == "1", timeout=5.0, sections=("dmr",),
                       what="the chip took the key")
    assert v.get("dmr.enc") in ("arc", "aes128", "aes256"), v
    # Never a key byte in any section: the driver's last report line shows no data for the key command
    d = driver(radio)
    assert not d.get("dmrc.last_report", "").startswith("81/") or d["dmrc.last_report"].endswith(":"), d
    radio.go_home()


@pytest.mark.feature("dmr_call")
@pytest.mark.radio_tx
def test_ptt(radio):
    """PTT on a DMR channel: the chip takes the call (0x06, PTT down), the call view goes to tx_voice, PTT up ends it.
    The second radio should show the call from this radio's ID to the channel's TX contact."""
    dmr_channel(radio)
    before = driver(radio)
    radio.link.ptt(True, 3000)
    try:
        core = radio.wait_for(lambda c: c.get("state") in ("tx", "tx_denied"), timeout=1.5, what="PTT taken")
        assert core.get("state") == "tx", core
        v = radio.wait_for(lambda v: v.get("dmr.phase") in ("tx_voice", "tx_denied"), timeout=2.0,
                           sections=("dmr",), what="the chip's answer to the call start")
        assert v.get("dmr.phase") == "tx_voice", v
        assert driver(radio).get("dmrc.on_air") == "1"
        time.sleep(1.5)
    finally:
        radio.link.ptt(False)
    radio.wait_for(lambda v: v.get("dmr.phase") == "idle", timeout=2.0, sections=("dmr",), what="the call's end")
    after = radio.wait_for(lambda d: d.get("dmrc.on_air") == "0", timeout=2.0, sections=("dmr_stats",),
                           what="PTT up answered")
    print("dmr:", {k: v[k] for k in ("dmr.target_id", "dmr.target_private") if k in v})
    assert after.get("dmrc.give_ups") == before.get("dmrc.give_ups"), after
    assert radio.view(["dmr"]).get("dmrc.ready") == "1"
    radio.go_home()
