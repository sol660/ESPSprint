"""Self-check for the board alert test (#20): runs it against a fake board and ntfy.

A healthy alert path must pass; a missing, stale, duplicate or wrong message,
and a failed send, must make the test FAIL (ADR 0001).
"""

import importlib.util
import time
from pathlib import Path

import pytest

import harness
import ntfy_rest
from fake_ntfy import DISCLAIMER, AlertingBoard, FakeNtfy, ntfy_test_body
from fake_serial import FakeFirmware, FakeSerial
from fake_tilt_firmware import FakeClock

ALERT = Path(__file__).resolve().parent.parent / "board" / "test_alert.py"
FW = harness.firmware_version()
TEST = "test_alert_command_sends_this_runs_test_message_to_ntfy"


def load_alert_tests():
    spec = importlib.util.spec_from_file_location("alert_under_check", ALERT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_alert(ntfy=None, port=None, **board_kw):
    """Runs the board alert test; returns the exception it raised, or None."""
    ntfy = ntfy or FakeNtfy()
    port = port or AlertingBoard(FW, ntfy, **board_kw)
    test = getattr(load_alert_tests(), TEST)
    try:
        test(harness.Board(port, clock=FakeClock(), sleep=lambda s: None), ntfy)
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def assert_failed(result):
    assert result is not None, "alert test PASSED against a bad board/ntfy: guard broken"
    assert isinstance(result, (AssertionError, pytest.fail.Exception)), (
        f"alert test must FAIL, but ended with {type(result).__name__}: {result}")


def test_passes_against_a_healthy_alert():
    # Positive control: the failures below must come from the checks, not a test that can't pass.
    assert run_alert() is None


def test_passes_with_other_messages_on_the_topic():
    ntfy = FakeNtfy()
    ntfy.post("Level WATCH at node-01\nTilt delta 1.20 deg\nTime 2026-10-24T00:00:00Z")
    ntfy.post(ntfy_test_body("111111111"), at=time.time() - 3600)

    assert run_alert(ntfy=ntfy) is None


def test_the_disclaimer_matches_the_firmware():
    text = (harness.ROOT / "lib" / "WakeCycle" / "src" / "AlertMessage.h").read_text(
        encoding="utf-8")
    assert '"TEST \\xE2\\x80\\x94 not a real alert"' in text
    assert load_alert_tests().DISCLAIMER == DISCLAIMER == "TEST — not a real alert"


def test_fails_when_no_message_reached_ntfy():
    assert_failed(run_alert(posts=0))


def test_fails_when_only_a_stale_message_is_there():
    # The board claims ok=1 but sends nothing; an older test message sits on the topic.
    ntfy = FakeNtfy()
    ntfy.post(ntfy_test_body("222222222"), at=time.time() - 600)

    assert_failed(run_alert(ntfy=ntfy, posts=0))


def test_fails_when_the_message_is_dated_before_the_test():
    assert_failed(run_alert(time_offset_s=-3600))


def test_fails_when_the_message_is_dated_in_the_future():
    assert_failed(run_alert(time_offset_s=3600))


def test_fails_when_the_message_has_another_number():
    assert_failed(run_alert(number="123456789"))


def test_fails_when_the_message_has_no_disclaimer():
    assert_failed(run_alert(disclaimer="Level ALERT at node-01"))


def test_fails_when_the_message_arrived_twice():
    assert_failed(run_alert(posts=2))


def test_fails_when_the_board_reports_a_failed_send():
    assert_failed(run_alert(ok=0))


def test_fails_against_a_silent_port():
    assert_failed(run_alert(port=FakeSerial()))


def test_fails_when_the_board_does_not_know_the_alert_command():
    assert_failed(run_alert(port=FakeFirmware(FW)))


def test_a_missing_message_is_polled_for_before_failing():
    ntfy = FakeNtfy()

    assert_failed(run_alert(ntfy=ntfy, posts=0))
    assert ntfy.polls > 1


# ---- secrets.h and config.h on the PC ----

EXAMPLE = harness.ROOT / "include" / "secrets.example.h"


def test_topic_is_read_from_the_firmware_header(tmp_path):
    header = tmp_path / "secrets.h"
    header.write_text(EXAMPLE.read_text(encoding="utf-8")
                      .replace("landslide-change-me-to-something-random", "landslide-x7k2q9"),
                      encoding="utf-8")

    ntfy = ntfy_rest.from_secrets(header)

    assert ntfy.topic == "landslide-x7k2q9"
    assert ntfy.server == "https://ntfy.sh"


def test_topic_still_holding_the_example_value_is_refused(tmp_path):
    header = tmp_path / "secrets.h"
    header.write_text(EXAMPLE.read_text(encoding="utf-8"), encoding="utf-8")

    with pytest.raises(ValueError, match="example"):
        ntfy_rest.from_secrets(header)


def test_missing_secrets_are_refused(tmp_path):
    with pytest.raises(ValueError, match="missing"):
        ntfy_rest.from_secrets(tmp_path / "secrets.h")
