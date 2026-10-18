"""Tilt reading on the real board, through the serial seam (#17; spec #14 stories 22-26).

The automatic tests need the board resting still on a firm surface. The manual
test (run with --manual) asks the installer to tilt it by hand.
"""

import pytest

import harness

BOOT_WITHIN_S = 5
# The first wake cycle runs before commands are read; with no stored baseline it
# can take up to 35 s (25 s baseline + 10 s reading).
# Since #19 the power-on wake also uploads its reading before the window opens
# (user approval on #14, 2026-10-16): add the worst-case upload from config.h.
FIRST_REPLY_WITHIN_S = 45 + harness.upload_worst_case_s()
READ_WITHIN_S = 15      # config::READING_TIME_LIMIT_MS (10 s) plus margin
BASELINE_WITHIN_S = 30  # config::BASELINE_TIME_LIMIT_MS (25 s) plus margin

# Uncalibrated MPU6050: zero-g offset up to ~50-80 mg per axis and +/-3 % scale.
# 0.1 g still catches a wrong range or scale (0.5 g, 2 g) and a dead axis.
ACCEL_TOLERANCE_G = 0.10
# The motion gate, config::STILL_ACCEL_STD_G. A still board's noise is above 0
# (exactly 0 means the sensor output is stuck) and within the gate.
NOISE_GATE_G = 0.003
# Straight after `baseline`, with the board untouched, temperature drift is nil and
# averaged noise is ~0.01 deg, so the delta should be well inside the 0.25 deg
# accuracy aim (ADR 0002; 0.5 deg is only accepted for drift over time).
DELTA_AFTER_BASELINE_DEG = 0.25
# Repeatability: back-to-back reads of an untouched board. Averaged noise is
# ~0.01 deg, so 0.1 deg spread (max - min) leaves room for bench vibration while
# staying well inside the accuracy aim. The measured spread is printed (-s).
REPEAT_READS = 5
REPEAT_SPREAD_DEG = 0.10
# Manual hand tilt: the installer is asked for 5-10 deg, so at least 3 deg proves
# the delta moved. Putting the board back by hand isn't exact, so the return is
# held to the 0.5 deg accepted accuracy (ADR 0002).
HAND_TILT_MIN_DEG = 3.0
RETURN_TOLERANCE_DEG = 0.5


def start(board):
    """Resets the board and waits until it answers commands."""
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)
    board.sync(FIRST_REPLY_WITHIN_S)


def read(board):
    r = board.ask("read", "read", READ_WITHIN_S)
    assert r.fields.get("ok") == "1", f"read failed: {r.line}"
    return r


def capture_baseline(board):
    s = board.ask("baseline", "status", BASELINE_WITHIN_S)
    assert s.fields.get("ok") == "1" and s.fields.get("baseline") == "1", (
        f"baseline capture failed (board moving, or flash write error): {s.line}")
    return s


def assert_delta_near_zero(r, tolerance_deg):
    assert r.fields.get("stable") == "1", f"reading not stable: {r.line}"
    for key in ("pitch", "roll", "tilt"):
        value = r.number(key)
        assert abs(value) <= tolerance_deg, (
            f"{key} delta {value} deg, expected 0 +/- {tolerance_deg} deg: {r.line}")


def test_still_board_reads_about_1g(board):
    start(board)

    r = read(board)

    g = r.number("g")
    assert abs(g - 1.0) <= ACCEL_TOLERANCE_G, (
        f"total acceleration {g} g, expected 1 +/- {ACCEL_TOLERANCE_G} g at rest "
        f"(wiring, sensor range or a dead axis?): {r.line}")


def test_still_board_gives_a_stable_reading(board):
    start(board)

    r = read(board)

    assert r.fields.get("stable") == "1", (
        f"motion gate rejected a still board (is it on a firm surface?): {r.line}")
    noise = r.number("noise")
    assert 0 < noise <= NOISE_GATE_G, (
        f"noise {noise} g, expected above 0 and within the {NOISE_GATE_G} g gate: {r.line}")


def test_delta_is_about_zero_right_after_baseline(board):
    start(board)
    capture_baseline(board)

    r = read(board)

    assert_delta_near_zero(r, DELTA_AFTER_BASELINE_DEG)


def test_repeated_reads_of_a_still_board_agree(board):
    start(board)

    reads = [read(board) for _ in range(REPEAT_READS)]

    for r in reads:
        assert r.fields.get("stable") == "1", f"reading not stable: {r.line}"
    for key in ("pitch", "roll"):
        values = [r.number(key) for r in reads]
        spread = max(values) - min(values)
        print(f"{key}: spread {spread:.3f} deg over {REPEAT_READS} reads {values}")
        assert spread <= REPEAT_SPREAD_DEG, (
            f"{key} spread {spread:.3f} deg over {REPEAT_READS} reads, expected at most "
            f"{REPEAT_SPREAD_DEG} deg: {values}")


def assert_tilted(r, key, sign, edge):
    """`key` moved at least HAND_TILT_MIN_DEG in direction `sign`, more than the other axis."""
    assert r.fields.get("stable") == "1", f"reading not stable (hold the board still): {r.line}"
    value = r.number(key)
    other = r.number("roll" if key == "pitch" else "pitch")
    assert sign * value >= HAND_TILT_MIN_DEG, (
        f"raising the {edge} edge should make {key} {'+' if sign > 0 else '-'}"
        f"{HAND_TILT_MIN_DEG} deg or more, got {value} deg: {r.line}")
    assert abs(value) > abs(other), (
        f"raising the {edge} edge moved the other axis more ({other} deg) than {key}: {r.line}")


@pytest.mark.manual
def test_hand_tilt_moves_the_delta_and_it_returns_to_zero(board, installer):
    """Sign conventions on the real mounting (spec #14, story 26).

    Axes are the MPU6050's X and Y arrows. Raising the +X edge makes pitch
    negative (pitch = atan2(-ax, ...)); raising the +Y edge makes roll positive.
    """
    start(board)
    installer.do("""
        Put the board flat on a firm table, in a spot you can return it to exactly
        (mark its corners). Let go: it must stay still while the baseline is taken (~25 s).
    """)
    capture_baseline(board)

    installer.do("""
        Raise the edge of the board that the MPU6050's +X arrow points to, by about
        5-10 deg (e.g. rest that edge on a pencil or a coin stack). Let go and keep it still.
    """)
    assert_tilted(read(board), "pitch", -1, "+X")

    installer.do("Put the board back flat on its marks. Let go and keep it still.")
    assert_delta_near_zero(read(board), RETURN_TOLERANCE_DEG)

    installer.do("""
        Raise the edge of the board that the MPU6050's +Y arrow points to, by about
        5-10 deg. Let go and keep it still.
    """)
    assert_tilted(read(board), "roll", +1, "+Y")

    installer.do("Put the board back flat on its marks. Let go and keep it still.")
    assert_delta_near_zero(read(board), RETURN_TOLERANCE_DEG)
