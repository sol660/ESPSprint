"""ntfy test alert from the real board (#20; spec #14 stories 50-53).

Needs the plugged-in board, a WiFi network from include/secrets.h in range, and
NTFY_TOPIC set in include/secrets.h. The PC reads the same topic and polls
ntfy's JSON API. Phones subscribed to the topic get the test message too; it
says "TEST — not a real alert".

Guards against a false pass (ADR 0001): the message must carry this run's
random number and the disclaimer, be the only such message, and have reached
ntfy after the test started.
"""

import time

import harness

BOOT_WITHIN_S = 5
# Before commands are read, the first wake cycle runs: up to 35 s of reading
# (with no stored baseline) and its upload: up to 2 x 30 s of WiFi, 5 s NTP,
# 10 s HTTPS (and 10 s more for a pending alert).
FIRST_REPLY_WITHIN_S = 35 + 75 + 20
# `alert`: up to 2 x 30 s to join, 5 s NTP, 10 s HTTPS, plus margin.
ALERT_WITHIN_S = 60 + 5 + 10 + 15
# The board said ok=1, so ntfy has it; give the poll a little time anyway.
POLL_ATTEMPTS = 10
POLL_INTERVAL_S = 1
# ntfy's clock and the PC's can differ a little.
CLOCK_SKEW_S = 5
DISCLAIMER = "TEST — not a real alert"
SETUP = "check the WiFi networks and NTFY_TOPIC in include/secrets.h"


def is_this_runs_message(msg, number):
    lines = msg.get("message", "").splitlines()
    return bool(lines) and lines[0] == DISCLAIMER and number in msg["message"].split()


def test_alert_command_sends_this_runs_test_message_to_ntfy(board, ntfy):
    board.reset()
    board.expect("boot", BOOT_WITHIN_S)
    board.sync(FIRST_REPLY_WITHIN_S)
    started = time.time()
    number = harness.random_id()

    board.send(f"alert {number}")

    reply = board.expect("alert", ALERT_WITHIN_S, match=lambda t: t.fields.get("tag") == number,
                         what=f"@alert tag={number} in reply to `alert {number}`")
    assert reply.fields.get("ok") == "1", f"test alert not sent ({SETUP}): {reply.line}"

    since = started - CLOCK_SKEW_S
    found, seen = [], []
    for attempt in range(POLL_ATTEMPTS):
        seen = ntfy.messages_since(since)
        found = [m for m in seen if is_this_runs_message(m, number)]
        if found:
            break
        ntfy.pause(POLL_INTERVAL_S)
    assert found, (
        f"no ntfy message on the topic with \"{DISCLAIMER}\" and {number}. "
        f"Messages since the test started: {[m.get('message') for m in seen]}")
    assert len(found) == 1, f"expected the test message once, ntfy has {len(found)}: {found}"

    msg = found[0]
    now = time.time()
    assert msg.get("time", 0) >= since, (
        f"the message is dated {msg.get('time')}, before the test started ({started:.0f}): "
        f"an old message? {msg}")
    assert msg["time"] <= now + CLOCK_SKEW_S, f"the message is dated in the future: {msg}"
