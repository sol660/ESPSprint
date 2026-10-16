"""Six-position accelerometer calibration on the real board (#22). Manual only (--manual).

The installer rests the board on each of its six faces in turn; the board
captures each one (`cal <face>`), then works out and saves the calibration
(`cal save`). A flat `read` afterwards must then give 1 g, corrected (cal=1).
Run it on the normal esp32dev firmware: each step waits for the installer, and
the esp32dev_hil build sleeps after 20 s without a command.

The board is left calibrated, with a fresh baseline taken flat at the end.

Guards against a false pass (ADR 0001): every reply follows a fresh ping, each
capture must report the face it was asked for and one more face than the last,
and the final read must say it was corrected.
"""

import pytest

import harness

BOOT_WITHIN_S = 5
FIRST_REPLY_WITHIN_S = 45 + harness.upload_worst_case_s()
CAL_WITHIN_S = 30       # a face is averaged like a baseline: config::BASELINE_TIME_LIMIT_MS (25 s) plus margin
READ_WITHIN_S = 15      # config::READING_TIME_LIMIT_MS (10 s) plus margin
BASELINE_WITHIN_S = 30  # config::BASELINE_TIME_LIMIT_MS (25 s) plus margin
# A calibrated still board: the six-position method removes offset and scale
# error to well under 1 %, so 0.02 g leaves room for faces that weren't exactly
# vertical (5 deg off costs 0.4 %) without passing an uncorrected 0.876 g.
CALIBRATED_G_TOLERANCE = 0.02
# Straight after `baseline` (as in test_reading.py).
DELTA_AFTER_BASELINE_DEG = 0.25

# Flat last, so the read and the baseline afterwards are taken flat.
FACES = [
    ("-z", """
        Turn the board upside down (component side facing the table), level, and
        resting still. Prop it on its edges if parts stick out; it must not rock.
    """),
    ("+x", "Stand the board on an edge so the MPU6050's +X arrow points straight up. Hold it still with a block, not your hand."),
    ("-x", "Stand the board on the opposite edge, so the +X arrow points straight down. Prop it still."),
    ("+y", "Stand the board on an edge so the MPU6050's +Y arrow points straight up. Prop it still."),
    ("-y", "Stand the board on the opposite edge, so the +Y arrow points straight down. Prop it still."),
    ("+z", "Put the board flat on the table, component side up, the way it is mounted. Let go."),
]


def start(board):
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)
    board.sync(FIRST_REPLY_WITHIN_S)


@pytest.mark.manual
def test_six_faces_calibrate_the_board_to_1g(board, installer):
    start(board)

    for count, (face, instructions) in enumerate(FACES, start=1):
        installer.do(instructions + f"\n(Face {count} of 6: {face} up. The capture takes ~25 s.)")
        r = board.ask(f"cal {face}", "cal", CAL_WITHIN_S)
        assert r.fields.get("ok") == "1", (
            f"capture of face {face} failed ({r.fields.get('err')}): {r.line}")
        assert r.fields.get("step") == face, f"reply is for another face: {r.line}"
        assert r.number("faces") == count, (
            f"expected {count} faces captured after {face}: {r.line}")

    saved = board.ask("cal save", "cal", READ_WITHIN_S)
    assert saved.fields.get("ok") == "1", (
        f"`cal save` refused ({saved.fields.get('err')}): {saved.line}")
    assert saved.fields.get("step") == "save", f"not a reply to `cal save`: {saved.line}"
    assert saved.fields.get("baseline") == "0", f"the old baseline was not erased: {saved.line}"
    for key in ("ox", "oy", "oz", "sx", "sy", "sz"):
        print(f"{key} = {saved.number(key)}")

    r = board.ask("read", "read", READ_WITHIN_S)
    assert r.fields.get("ok") == "1" and r.fields.get("stable") == "1", (
        f"flat read failed or unstable: {r.line}")
    assert r.fields.get("cal") == "1", f"the reading was not corrected: {r.line}"
    g = r.number("g")
    assert abs(g - 1.0) <= CALIBRATED_G_TOLERANCE, (
        f"calibrated total {g} g, expected 1 +/- {CALIBRATED_G_TOLERANCE} g: {r.line}")

    s = board.ask("baseline", "status", BASELINE_WITHIN_S)
    assert s.fields.get("ok") == "1" and s.fields.get("baseline") == "1", (
        f"baseline after calibration failed: {s.line}")
    r = board.ask("read", "read", READ_WITHIN_S)
    for key in ("pitch", "roll", "tilt"):
        assert abs(r.number(key)) <= DELTA_AFTER_BASELINE_DEG, (
            f"{key} delta {r.number(key)} deg after the new baseline: {r.line}")
