"""Self-check for the board calibration test (#22): runs it against fake firmware.

A healthy board must pass; a board that is silent, or wrong in one specific
way, must make the test FAIL (ADR 0001).
"""

import importlib.util
from pathlib import Path

import pytest

import harness
from fake_cal_firmware import GRAVITY, CalBoard
from fake_serial import FakeFirmware, FakeSerial
from fake_tilt_firmware import FakeClock, FakeInstaller

CALIBRATION = Path(__file__).resolve().parent.parent / "board" / "test_calibration.py"
FW = harness.firmware_version()
TEST = "test_six_faces_calibrate_the_board_to_1g"


def load_calibration_tests():
    spec = importlib.util.spec_from_file_location("calibration_under_check", CALIBRATION)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def turning(board, faces=None):
    """A fake installer whose steps turn the fake board to each face the test asks for."""
    module = load_calibration_tests()
    faces = faces or [face for face, _ in module.FACES]
    return FakeInstaller(*[lambda f=f: setattr(board, "up", f) for f in faces])


def run(port, installer):
    test = getattr(load_calibration_tests(), TEST)
    try:
        test(harness.Board(port, clock=FakeClock(), sleep=lambda s: None), installer)
    except BaseException as e:  # noqa: BLE001 - a skip must be caught and reported too
        return e
    return None


def assert_failed(outcome):
    assert outcome is not None, "board test PASSED against a bad board: guard broken"
    assert isinstance(outcome, (AssertionError, pytest.fail.Exception)), (
        f"board test must FAIL, but ended with {type(outcome).__name__}: {outcome}")


def test_calibration_test_is_manual():
    marks = getattr(getattr(load_calibration_tests(), TEST), "pytestmark", [])

    assert "manual" in [m.name for m in marks]


def test_the_test_covers_all_six_faces_and_ends_flat():
    faces = [face for face, _ in load_calibration_tests().FACES]

    assert sorted(faces) == sorted(GRAVITY)
    assert faces[-1] == "+z"


def test_passes_against_a_healthy_board():
    board = CalBoard(FW)
    installer = turning(board)

    assert run(board, installer) is None
    assert len(installer.steps) == 6
    assert board.cal is not None and board.baseline is not None


def test_the_fake_sensor_reads_low_uncorrected():
    # Positive control for the cal=0 checks below: uncorrected, it is far from 1 g.
    board = CalBoard(FW)
    board.emit = lambda line: setattr(board, "last", line)
    board.on_write(b"read\n")

    assert "cal=0" in board.last
    g = float(board.last.split("g=")[1].split()[0])
    assert abs(g - 1.0) > 0.1


def test_fails_against_a_silent_port():
    assert_failed(run(FakeSerial(), FakeInstaller()))


def test_fails_against_a_board_without_cal():
    assert_failed(run(FakeFirmware(FW), FakeInstaller()))


def test_fails_when_a_capture_is_refused():
    board = CalBoard(FW, capture_ok=False)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_face_count_does_not_go_up():
    board = CalBoard(FW, faces_stuck=True)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_reply_is_for_another_face():
    board = CalBoard(FW, wrong_step=True)
    assert_failed(run(board, turning(board)))


def test_fails_when_save_is_refused():
    board = CalBoard(FW, save_ok=False)
    assert_failed(run(board, turning(board)))


def test_fails_when_save_keeps_the_old_baseline():
    board = CalBoard(FW, erases_baseline=False)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_reading_is_not_corrected():
    board = CalBoard(FW, applies_cal=False, claims_cal=False)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_board_claims_a_correction_it_did_not_make():
    board = CalBoard(FW, applies_cal=False, claims_cal=True)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_correction_reports_cal0():
    board = CalBoard(FW, applies_cal=True, claims_cal=False)
    assert_failed(run(board, turning(board)))


@pytest.mark.parametrize("error", [0.03, -0.03])
def test_fails_when_the_calibrated_total_is_off(error):
    board = CalBoard(FW, g_error=error)
    assert_failed(run(board, turning(board)))


def test_passes_with_a_small_calibrated_error():
    board = CalBoard(FW, g_error=0.01)
    assert run(board, turning(board)) is None


def test_fails_when_the_installer_left_the_board_on_the_wrong_face():
    # Every capture of the real board would still say ok=1 here (the fake has
    # no face check), but the final flat read is taken on its side: g is wrong.
    board = CalBoard(FW)
    faces = [face for face, _ in load_calibration_tests().FACES][:-1] + ["+x"]
    assert_failed(run(board, turning(board, faces)))


def test_fails_when_the_new_baseline_is_refused():
    board = CalBoard(FW, baseline_ok=False)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_new_baseline_is_not_subtracted():
    board = CalBoard(FW, subtracts_baseline=False)
    assert_failed(run(board, turning(board)))


def test_fails_when_the_flat_read_is_unstable():
    board = CalBoard(FW, stable=0)
    assert_failed(run(board, turning(board)))
