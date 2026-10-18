"""Self-check for the board reading tests (#17): runs them against fake firmware.

A healthy still board must pass; a board that is silent, stale, or wrong in one
specific way must make the test FAIL (ADR 0001).
"""

import importlib.util
from pathlib import Path

import pytest

import harness
from fake_serial import FakeFirmware, FakeSerial
from fake_tilt_firmware import BoardThatPrintsAReadingAtBoot, FakeClock, FakeInstaller, StillBoard

READING = Path(__file__).resolve().parent.parent / "board" / "test_reading.py"
FW = harness.firmware_version()

AUTOMATIC = [
    "test_still_board_reads_about_1g",
    "test_still_board_gives_a_stable_reading",
    "test_delta_is_about_zero_right_after_baseline",
    "test_repeated_reads_of_a_still_board_agree",
]


def load_reading_tests():
    spec = importlib.util.spec_from_file_location("reading_under_check", READING)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(name, port, *extra):
    """Runs one real board test against `port`; returns the exception it raised, or None."""
    test = getattr(load_reading_tests(), name)
    try:
        test(harness.Board(port, clock=FakeClock(), sleep=lambda s: None), *extra)
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def assert_failed(outcome):
    assert outcome is not None, "board test PASSED against a bad board: guard broken"
    assert isinstance(outcome, (AssertionError, pytest.fail.Exception)), (
        f"board test must FAIL, but ended with {type(outcome).__name__}: {outcome}")


@pytest.mark.parametrize("name", AUTOMATIC)
def test_passes_against_a_healthy_still_board(name):
    # Positive control: the failures below must come from the checks, not a test that can't pass.
    assert run(name, StillBoard(FW)) is None


@pytest.mark.parametrize("name", AUTOMATIC)
def test_fails_against_a_silent_port(name):
    assert_failed(run(name, FakeSerial()))


@pytest.mark.parametrize("name", AUTOMATIC)
def test_fails_when_the_board_ignores_the_command(name):
    stale = ["@read ok=1 pitch=0.000 roll=0.000 tilt=0.000 stable=1 noise=0.00100 temp=24.0 g=1.0000"]
    assert_failed(run(name, FakeFirmware(FW, stale_lines=stale)))


@pytest.mark.parametrize("name", AUTOMATIC)
def test_fails_when_the_only_reply_was_printed_before_the_command(name):
    assert_failed(run(name, BoardThatPrintsAReadingAtBoot(FW)))


@pytest.mark.parametrize("g", [0.5, 0.85, 1.15, 2.0])
def test_1g_check_fails_when_the_total_is_off(g):
    assert_failed(run("test_still_board_reads_about_1g", StillBoard(FW, g=g)))


def test_stable_check_fails_when_the_motion_gate_rejects_the_reading():
    assert_failed(run("test_still_board_gives_a_stable_reading", StillBoard(FW, stable=0)))


@pytest.mark.parametrize("noise", [0.0, 0.0035])
def test_stable_check_fails_on_frozen_or_too_noisy_data(noise):
    # 0 noise means the sensor output is stuck; above the 0.003 g gate means it isn't still.
    assert_failed(run("test_still_board_gives_a_stable_reading", StillBoard(FW, noise=noise)))


def test_baseline_check_fails_when_the_baseline_is_not_subtracted():
    board = StillBoard(FW, raw=(1.2, -0.7), subtracts_baseline=False)

    assert_failed(run("test_delta_is_about_zero_right_after_baseline", board))


def test_baseline_check_fails_when_the_capture_fails():
    board = StillBoard(FW, raw=(0.0, 0.0), baseline_ok=False)

    assert_failed(run("test_delta_is_about_zero_right_after_baseline", board))


def test_repeat_check_fails_when_reads_scatter():
    assert_failed(run("test_repeated_reads_of_a_still_board_agree", StillBoard(FW, jitter=0.3)))


def test_repeat_check_passes_with_small_scatter():
    assert run("test_repeated_reads_of_a_still_board_agree", StillBoard(FW, jitter=0.02)) is None


HAND_TILT = "test_hand_tilt_moves_the_delta_and_it_returns_to_zero"
FLAT = (1.2, -0.7)  # the bench isn't perfectly level; the baseline takes that out


def hand(board, *raws):
    """A fake installer whose steps put the fake board at each raw (pitch, roll) in turn."""
    return FakeInstaller(*[lambda raw=raw: setattr(board, "raw", raw) for raw in raws])


def x_edge_up(deg):
    # Raising the +X edge points +X up: ax = +sin, so pitch = atan2(-ax, ...) goes negative.
    return (FLAT[0] - deg, FLAT[1])


def y_edge_up(deg):
    # Raising the +Y edge: ay = +sin, so roll = atan2(ay, az) goes positive.
    return (FLAT[0], FLAT[1] + deg)


def test_hand_tilt_test_is_manual():
    marks = getattr(getattr(load_reading_tests(), HAND_TILT), "pytestmark", [])

    assert "manual" in [m.name for m in marks]


def test_hand_tilt_passes_when_the_delta_moves_the_right_way_and_returns():
    board = StillBoard(FW, raw=FLAT)
    installer = hand(board, FLAT, x_edge_up(8), FLAT, y_edge_up(8), FLAT)

    assert run(HAND_TILT, board, installer) is None
    assert len(installer.steps) == 5


def test_hand_tilt_fails_when_pitch_moves_the_wrong_way():
    board = StillBoard(FW, raw=FLAT)
    installer = hand(board, FLAT, x_edge_up(-8), FLAT, y_edge_up(8), FLAT)

    assert_failed(run(HAND_TILT, board, installer))


def test_hand_tilt_fails_when_roll_moves_the_wrong_way():
    board = StillBoard(FW, raw=FLAT)
    installer = hand(board, FLAT, x_edge_up(8), FLAT, y_edge_up(-8), FLAT)

    assert_failed(run(HAND_TILT, board, installer))


def test_hand_tilt_fails_when_the_board_did_not_move():
    board = StillBoard(FW, raw=FLAT)
    installer = hand(board, FLAT, FLAT, FLAT, FLAT, FLAT)

    assert_failed(run(HAND_TILT, board, installer))


def test_hand_tilt_fails_when_the_delta_does_not_return_to_zero():
    board = StillBoard(FW, raw=FLAT)
    installer = hand(board, FLAT, x_edge_up(8), x_edge_up(1), y_edge_up(8), FLAT)

    assert_failed(run(HAND_TILT, board, installer))


def test_baseline_check_fails_when_the_reading_after_it_is_unstable():
    board = StillBoard(FW, stable=0)

    assert_failed(run("test_delta_is_about_zero_right_after_baseline", board))
