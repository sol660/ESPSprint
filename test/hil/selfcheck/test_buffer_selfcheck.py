"""Self-check for the board buffer tests (#7): runs them against a fake firmware.

A healthy board must pass; a board that is silent, stale, or wrong in one
specific way must make the test FAIL (ADR 0001).
"""

import importlib.util
from pathlib import Path

import pytest

import harness
from fake_buffer_firmware import BufferBoard
from fake_serial import FakeFirmware, FakeSerial
from fake_tilt_firmware import FakeClock

BUFFER = Path(__file__).resolve().parent.parent / "board" / "test_buffer.py"
FW = harness.firmware_version()

WAKE = "test_every_wake_prints_the_buffer_line"
STATUS = "test_status_reports_the_buffer"
CLEARBUF = "test_clearbuf_empties_the_buffer"
ALL = [WAKE, STATUS, CLEARBUF]


def load_buffer_tests():
    spec = importlib.util.spec_from_file_location("buffer_under_check", BUFFER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(name, port):
    """Runs one real board test against `port`; returns the exception it raised, or None."""
    test = getattr(load_buffer_tests(), name)
    try:
        test(harness.Board(port, clock=FakeClock(), sleep=lambda s: None))
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def assert_failed(outcome):
    assert outcome is not None, "board test PASSED against a bad board: guard broken"
    assert isinstance(outcome, (AssertionError, pytest.fail.Exception)), (
        f"board test must FAIL, but ended with {type(outcome).__name__}: {outcome}")


# ---- positive controls: the failures below must come from the checks ----

@pytest.mark.parametrize("name", ALL)
@pytest.mark.parametrize("online", [True, False])
def test_passes_against_a_healthy_board(name, online):
    assert run(name, BufferBoard(FW, online=online)) is None


def test_clearbuf_passes_with_an_empty_buffer_too():
    assert run(CLEARBUF, BufferBoard(FW, count=0, online=True)) is None


# ---- silent, stale or old firmware ----

@pytest.mark.parametrize("name", ALL)
def test_fails_against_a_silent_port(name):
    assert_failed(run(name, FakeSerial()))


@pytest.mark.parametrize("name", ALL)
def test_fails_against_a_board_with_no_buffer(name):
    # Answers ping, but prints no @upload, @buffer or @clearbuf at all.
    assert_failed(run(name, FakeFirmware(FW)))


@pytest.mark.parametrize("name", ALL)
def test_fails_when_the_only_lines_are_stale(name):
    stale = ["@upload ok=1 tag=none seq=1 boot=1",
             "@buffer count=0 sent=1 dropped=0 skipped=0",
             "@clearbuf ok=1 cleared=0 count=0"]
    assert_failed(run(name, FakeFirmware(FW, stale_lines=stale)))


def test_status_check_fails_when_the_only_buffer_line_was_the_wakes():
    # The boot wake prints @buffer, but `status` doesn't: that line came before the ping.
    assert_failed(run(STATUS, BufferBoard(FW, status_line=False)))


# ---- the wake's line ----

def test_wake_check_fails_with_no_buffer_line_after_the_upload():
    assert_failed(run(WAKE, BufferBoard(FW, wake_line=False)))


@pytest.mark.parametrize("line", [
    "@buffer count=0 sent=1 dropped=0",            # before #7's follow-up: no skipped=
    "@buffer sent=1 dropped=0 skipped=0",          # no count=
    "@buffer count=-1 sent=1 dropped=0 skipped=0",  # not a count
    "@buffer count=x sent=1 dropped=0 skipped=0",
])
def test_wake_check_fails_on_a_missing_or_bad_key(line):
    assert_failed(run(WAKE, BufferBoard(FW, wake_line=line)))


def test_wake_check_fails_when_an_uploaded_reading_is_not_counted_as_sent():
    assert_failed(run(WAKE, BufferBoard(FW, online=True,
                                        wake_line="@buffer count=0 sent=0 dropped=0 skipped=0")))


def test_wake_check_fails_when_an_unsent_reading_is_not_buffered():
    assert_failed(run(WAKE, BufferBoard(FW, online=False, count=0,
                                        wake_line="@buffer count=0 sent=0 dropped=0 skipped=0")))


# ---- status ----

@pytest.mark.parametrize("line", [
    "@buffer count=4 sent=0 dropped=0",
    "@buffer count=4 sent=2 dropped=0 skipped=0",
    "@buffer count=4 sent=0 dropped=1 skipped=0",
    "@buffer count=4 sent=0 dropped=0 skipped=1",
])
def test_status_check_fails_on_a_missing_key_or_a_count_status_cant_have(line):
    assert_failed(run(STATUS, BufferBoard(FW, online=False, status_line=line)))


# ---- clearbuf ----

def test_clearbuf_check_fails_when_the_board_does_not_know_clearbuf():
    assert_failed(run(CLEARBUF, BufferBoard(FW, online=False, knows_clearbuf=False)))


def test_clearbuf_check_fails_when_clearbuf_reports_a_flash_error():
    assert_failed(run(CLEARBUF, BufferBoard(FW, online=False, clear_ok=False)))


def test_clearbuf_check_fails_when_readings_are_left():
    assert_failed(run(CLEARBUF, BufferBoard(FW, online=False, clears=False)))


def test_clearbuf_check_fails_when_it_claims_to_clear_but_status_still_counts_them():
    board = BufferBoard(FW, online=False, clears=False, reported_cleared=4, reported_count=0)

    assert_failed(run(CLEARBUF, board))


def test_clearbuf_check_fails_when_the_cleared_number_is_wrong():
    assert_failed(run(CLEARBUF, BufferBoard(FW, online=False, reported_cleared=0)))


def test_clearbuf_check_fails_when_status_has_no_buffer_line():
    assert_failed(run(CLEARBUF, BufferBoard(FW, online=False, status_line=False)))
