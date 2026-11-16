# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Inspect the last owner-operated microphone preparation; never key or retune."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("dmr")
def test_codec_mic_gains_acknowledged(radio):
    view = radio.view(["board", "core"])
    prefix = "board.codec_runtime."
    if prefix + "acked" not in view:
        skip("no_hw", "board has no acknowledged microphone preparation diagnostics")
    if view.get("state") not in ("idle", "rx"):
        skip("state", "codec preparation check requires receive mode")
    assert view[prefix + "fault"] == "0", "codec stream timed out or failed to send; restart the modem"
    assert view[prefix + "error"] == "0", "codec refused a runtime write"
    if int(view[prefix + "acked"]) != 4:
        skip("state", "no complete owner-operated microphone preparation since boot")
    assert view[prefix + "mic_pga"] == view[prefix + "mic_ceiling"], "acknowledged PGA and AGC ceiling differ"
