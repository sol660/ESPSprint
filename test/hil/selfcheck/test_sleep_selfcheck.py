"""Self-check for the board sleep tests (#18): runs them against a fake sleeping board.

A healthy esp32dev_hil board must pass; a board that is silent, never sleeps,
resets instead of sleeping, or gets the command window wrong must make the
test FAIL (ADR 0001).
"""

import importlib.util
from pathlib import Path

import pytest

import harness
from fake_serial import FakeFirmware, FakeSerial
from fake_sleepy_firmware import SharedClock, SleepyBoard

SLEEP = Path(__file__).resolve().parent.parent / "board" / "test_sleep.py"
FW = harness.firmware_version()

TESTS = [
    "test_sleeps_after_the_window_and_wakes_on_the_timer",
    "test_a_command_keeps_the_board_awake_past_the_initial_window",
]


def run(name, make_port):
    """Runs one real board test against the port `make_port(clock)` builds.

    Returns the exception it raised, or None."""
    spec = importlib.util.spec_from_file_location("sleep_under_check", SLEEP)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    clock = SharedClock()
    port = make_port(clock)
    try:
        getattr(module, name)(harness.Board(port, clock=clock, sleep=lambda s: None))
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def board(**kw):
    return lambda clock: SleepyBoard(FW, clock, **kw)


def assert_failed(outcome):
    assert outcome is not None, "sleep test PASSED against a bad board: guard broken"
    assert isinstance(outcome, (AssertionError, pytest.fail.Exception)), (
        f"sleep test must FAIL, but ended with {type(outcome).__name__}: {outcome}")


@pytest.mark.parametrize("name", TESTS)
def test_passes_against_a_healthy_hil_board(name):
    # Positive control: the failures below must come from the checks, not a test that can't pass.
    assert run(name, board()) is None


@pytest.mark.parametrize("name", TESTS)
def test_passes_with_a_slow_first_cycle(name):
    # First boot with no stored baseline: up to 35 s before the window opens.
    assert run(name, board(cycle_s=35.0)) is None


@pytest.mark.parametrize("name", TESTS)
def test_fails_against_a_silent_port(name):
    assert_failed(run(name, lambda clock: FakeSerial()))


@pytest.mark.parametrize("name", TESTS)
def test_fails_against_a_board_that_never_sleeps(name):
    assert_failed(run(name, board(sleeps=False)))


@pytest.mark.parametrize("name", TESTS)
def test_fails_against_a_board_that_never_sleeps_and_only_answers_pings(name):
    assert_failed(run(name, lambda clock: FakeFirmware(fw=FW)))


@pytest.mark.parametrize("name", TESTS)
def test_fails_against_the_normal_esp32dev_firmware(name):
    # 60 s window, 5 min idle, 15 min interval: not the build these tests need.
    assert_failed(run(name, board(window_s=60.0, idle_s=300.0, interval_s=900)))


@pytest.mark.parametrize("cause", ["PowerOn", "Motion", "Serial"])
def test_fails_when_the_wake_after_sleep_is_not_a_timer_wake(cause):
    # PowerOn here is a board that resets (crash, brown-out) instead of sleeping.
    assert_failed(run(TESTS[0], board(timer_cause=cause)))


def test_fails_when_the_board_wakes_long_before_its_timer():
    assert_failed(run(TESTS[0], board(wakes_after_s=3)))


def test_fails_when_the_board_wakes_long_after_its_timer():
    assert_failed(run(TESTS[0], board(wakes_after_s=90)))


def test_fails_when_the_sleep_line_gives_the_wrong_interval():
    assert_failed(run(TESTS[0], board(sleep_line_s=900)))


def test_fails_when_the_board_sleeps_without_a_command_window():
    assert_failed(run(TESTS[0], board(window_s=0.0)))


def test_fails_when_rtc_state_is_lost_across_sleep():
    assert_failed(run(TESTS[0], board(timer_boot_step=0)))


def test_fails_when_a_timer_wake_listens_for_commands():
    assert_failed(run(TESTS[0], board(listens_on_timer_wake=True)))


def test_fails_when_a_command_does_not_keep_the_board_awake():
    assert_failed(run(TESTS[1], board(commands_extend=False)))


def test_fails_when_the_board_stays_awake_long_after_the_last_command():
    assert_failed(run(TESTS[1], board(idle_s=60.0)))


def test_fails_when_the_board_sleeps_too_soon_after_the_last_command():
    assert_failed(run(TESTS[1], board(idle_s=16.0)))
