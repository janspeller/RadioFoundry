# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""Read tuning PWM registers against the current receive calibration; never key or retune."""
import pytest

from rfoundry_hil import skip


@pytest.mark.feature("fm")
def test_tuning_pwm_matches_calibration(radio):
    view = radio.view(["board", "core"])
    if "board.tuning.pwm_ready" not in view:
        skip("no_hw", "board has no tuning PWM register diagnostics")
    if view.get("state") not in ("idle", "rx"):
        skip("state", "tuning PWM comparison requires receive mode")
    expected = int(view["board.tuning.pwm_expected"])
    if expected == 65535:
        skip("state", "receive tuning calibration is unavailable")
    assert view["board.tuning.pwm_ready"] == "1", "tuning timer or output is disabled"
    assert int(view["board.tuning.pwm_compare"]) == expected, "PWM compare differs from receive calibration"
