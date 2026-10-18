"""Hello world for the board: proves flashing, reset, the serial link and the harness work."""

import random

import harness

BOOT_WITHIN_S = 5
# The first wake cycle runs before commands are read; with no stored baseline it
# can take up to 35 s (25 s baseline + 10 s reading).
# Since #19 the power-on wake also uploads its reading before the window opens
# (user approval on #14, 2026-10-16): add the worst-case upload from config.h.
PONG_WITHIN_S = 45 + harness.upload_worst_case_s()


def test_hello(board):
    board.reset()

    boot = board.expect("boot", BOOT_WITHIN_S)
    assert boot.fields.get("fw") == harness.firmware_version(), (
        f"board runs fw={boot.fields.get('fw')}, this checkout is "
        f"{harness.firmware_version()}: flash it first (pio run -t upload)")

    n = str(random.SystemRandom().randrange(10**8, 10**9))
    board.send(f"ping {n}")
    board.expect("pong", PONG_WITHIN_S, match=lambda t: t.args == [n], what=f"@pong {n}")
