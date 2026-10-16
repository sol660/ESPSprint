"""A fake ntfy topic and a fake board whose `alert` command posts to it (#20).

Both are built from attributes the self-check sets, so a check can make the
alert path healthy, or broken in one specific way.
"""

import re
import time

from fake_serial import FakeFirmware

DISCLAIMER = "TEST — not a real alert"


def ntfy_test_body(number, disclaimer=DISCLAIMER):
    """The body the firmware sends for `alert <number>` (AlertMessage.cpp)."""
    return (f"{disclaimer}\nTest number {number} from node-01\n"
            "Current level OK, tilt delta 0.12 deg\nTime 2026-10-24T00:00:00Z")


class FakeNtfy:
    """One topic. Like a server that ignores `since=`, it returns every cached
    message, so the test's own date check is what stops a stale message."""

    def __init__(self):
        self.messages = []
        self.polls = 0

    def post(self, message, at=None):
        self.messages.append({"id": f"m{len(self.messages)}", "event": "message",
                              "time": int(time.time() if at is None else at),
                              "topic": "t", "message": message})

    def messages_since(self, since_unix):
        self.polls += 1
        return [dict(m) for m in self.messages]

    def pause(self, seconds):
        pass


class AlertingBoard(FakeFirmware):
    """A board whose `alert <n>` prints `@alert` and posts a message to `ntfy`.

    Healthy: one message with the disclaimer and the number, dated now.
    """

    def __init__(self, fw, ntfy, posts=1, ok=1, number=None, disclaimer=DISCLAIMER,
                 time_offset_s=0.0, **kw):
        super().__init__(fw, **kw)
        self.ntfy = ntfy
        self.posts = posts
        self.ok = ok
        self.number = number  # post this number instead of the one asked for
        self.disclaimer = disclaimer
        self.time_offset_s = time_offset_s

    def on_write(self, data):
        super().on_write(data)  # ping
        m = re.match(rb"alert (\S+)\n", data)
        if not m:
            return
        n = m.group(1).decode()
        self.emit(f"alert: connecting, then sending test alert {n} to ntfy (up to ~80 s)...")
        for _ in range(self.posts):
            self.ntfy.post(ntfy_test_body(self.number or n, self.disclaimer),
                           at=time.time() + self.time_offset_s)
        self.emit(f"test alert {n} sent to ntfy")
        self.emit(f"@alert ok={self.ok} tag={n} level=OK tilt=0.120")
