"""Offline buffer lines on the real board (#7): `@buffer` and `clearbuf`.

Needs only the plugged-in board (normal `esp32dev` build); WiFi may be up or
down. `clearbuf` EMPTIES the board's offline buffer: readings waiting there are
lost, so don't run this on a board whose backlog still matters.

Guards against a false pass (ADR 0001): every line counted is read after this
boot (`reset`) or after a fresh ping (`ask`), so an old line can't answer. The
wake's `@buffer` has to agree with its `@upload`; `clearbuf` has to clear as
many readings as `status` reported, and a `status` after it has to say 0.
"""

import harness

BOOT_WITHIN_S = 5
# Before commands are read, the first wake cycle runs: up to 35 s of reading
# (with no stored baseline) and its upload (WiFi, NTP and HTTPS limits from config.h).
FIRST_REPLY_WITHIN_S = 35 + harness.upload_worst_case_s()
# `@buffer` is printed straight after `@upload` (wake) or `@status` (`status`).
LINE_AFTER_WITHIN_S = 5
# `status` and `clearbuf` only touch flash: no sensor, no WiFi.
COMMAND_WITHIN_S = 10
BUFFER_KEYS = ("count", "sent", "dropped", "skipped")


def start(board):
    """Resets the board and waits until it answers commands."""
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)
    board.sync(FIRST_REPLY_WITHIN_S)


def counts(t, keys):
    """The whole-number values of `keys` on a tagged line; fails if one is missing or not one."""
    values = {}
    for key in keys:
        value = t.fields.get(key)
        assert value is not None, f"no {key}= in: {t.line}"
        assert value.isdigit(), f"{key}= is not a whole number >= 0 in: {t.line}"
        values[key] = int(value)
    return values


def status_buffer(board):
    """`status`, then the `@buffer` line printed after its `@status`."""
    s = board.ask("status", "status", COMMAND_WITHIN_S)
    assert s.fields.get("ok") == "1", f"status failed: {s.line}"
    b = board.expect("buffer", LINE_AFTER_WITHIN_S, what="@buffer after @status")
    return counts(b, BUFFER_KEYS), b


def test_every_wake_prints_the_buffer_line(board):
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)

    up = board.expect("upload", FIRST_REPLY_WITHIN_S, what="the wake's @upload")
    b = board.expect("buffer", LINE_AFTER_WITHIN_S, what="@buffer after the wake's @upload")

    c = counts(b, BUFFER_KEYS)
    if up.fields.get("ok") == "1":
        assert c["sent"] >= 1, f"the wake's reading went up, so sent= must count it: {b.line}"
    else:
        assert up.fields.get("ok") == "0", f"@upload ok= is neither 0 nor 1: {up.line}"
        assert c["count"] >= 1, f"the wake's reading wasn't sent, so it must be buffered: {b.line}"


def test_status_reports_the_buffer(board):
    start(board)

    c, b = status_buffer(board)

    for key in ("sent", "dropped", "skipped"):
        assert c[key] == 0, f"`status` sends and drops nothing, so {key}= must be 0: {b.line}"


def test_clearbuf_empties_the_buffer(board):
    start(board)
    before, _ = status_buffer(board)

    cb = board.ask("clearbuf", "clearbuf", COMMAND_WITHIN_S)

    c = counts(cb, ("cleared", "count"))
    assert cb.fields.get("ok") == "1", f"clearbuf failed (flash error?): {cb.line}"
    assert c["count"] == 0, f"clearbuf left readings in the buffer: {cb.line}"
    assert c["cleared"] == before["count"], (
        f"status said {before['count']} buffered, but clearbuf cleared {c['cleared']}: {cb.line}")
    after, b = status_buffer(board)
    assert after["count"] == 0, f"status still counts buffered readings after clearbuf: {b.line}"
