"""A fake esp32dev_hil board with a command window and deep sleep (#18).

It runs on a shared fake clock: lines become readable only once the clock has
reached their time. Its timings and behaviour come from attributes, so the
self-check can make it healthy, or broken in one specific way.
"""

import re

from fake_serial import FakeFirmware


class SharedClock:
    """A fake clock that moves on 50 ms each time the harness looks at it.

    The fake board reads `.t` directly, so only the harness moves time on.
    """

    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 0.05
        return self.t


class SleepyBoard(FakeFirmware):
    """Healthy model of the esp32dev_hil firmware (include/config.h HIL timings).

    After a reset: `@boot cause=PowerOn`, the wake cycle (cycle_s), then the
    command window. Each handled command keeps it awake idle_s after it. When the
    window closes: `@sleep s=<interval>`, deep sleep, then `@boot cause=Timer`,
    one cycle, and straight back to sleep without listening.
    """

    def __init__(self, fw, clock, window_s=10.0, idle_s=20.0, interval_s=30, cycle_s=3.0,
                 timer_cycle_s=3.0, sleep_line_s=None, wakes_after_s=None, timer_cause="Timer",
                 timer_boot_step=1, sleeps=True, commands_extend=True,
                 listens_on_timer_wake=False, **kw):
        super().__init__(fw, **kw)
        self.clock = clock
        self.window_s = window_s
        self.idle_s = idle_s
        self.interval_s = interval_s
        self.cycle_s = cycle_s  # first wake cycle, before the window opens
        self.timer_cycle_s = timer_cycle_s  # a timer wake: baseline already stored
        self.sleep_line_s = interval_s if sleep_line_s is None else sleep_line_s
        self.wakes_after_s = interval_s if wakes_after_s is None else wakes_after_s
        self.timer_cause = timer_cause
        self.timer_boot_step = timer_boot_step  # 0 = boot count lost across sleep
        self.sleeps = sleeps
        self.commands_extend = commands_extend
        self.listens_on_timer_wake = listens_on_timer_wake
        self.pending = []      # (time, line), readable once the clock reaches time
        self.listen_from = None
        self.sleep_at = None   # when the open window closes; None: no sleep due
        self.slept = False
        self.boot = 0

    def at(self, t, line):
        self.pending.append((t, line))
        self.pending.sort(key=lambda p: p[0])

    def on_boot(self):
        now = self.clock.t
        self.pending.clear()
        self.boot = 1
        self.slept = False
        self.at(now + 0.3, "ets Jun  8 2016 00:22:57")
        self.at(now + 0.3, f"@boot fw={self.fw} boot={self.boot} cause=PowerOn")
        self.listen_from = now + 0.3 + self.cycle_s
        self.sleep_at = self.listen_from + self.window_s if self.sleeps else None

    def _advance(self):
        """Goes to sleep once the window has closed, and schedules the timer wakes."""
        if self.sleep_at is None or self.clock.t < self.sleep_at:
            return
        t = self.sleep_at
        self.sleep_at = None
        self.slept = True
        for _ in range(3):
            self.at(t, f"@sleep s={self.sleep_line_s}")
            t += self.wakes_after_s
            self.boot += self.timer_boot_step
            self.at(t, f"@boot fw={self.fw} boot={self.boot} cause={self.timer_cause}")
            t += self.timer_cycle_s

    def readline(self):
        self._advance()
        if self.pending and self.pending[0][0] <= self.clock.t:
            return (self.pending.pop(0)[1] + "\r\n").encode()
        return b""

    def on_write(self, data):
        self._advance()
        m = re.match(rb"ping (\S+)\n", data)
        if not m or self.listen_from is None:
            return
        now = self.clock.t
        pong = "@pong " + m.group(1).decode()
        if self.slept:
            # Asleep, or a timer wake: nobody listens (unless this fake is broken that way).
            if self.listens_on_timer_wake:
                self.at(now + 0.05, pong)
            return
        handled = max(now, self.listen_from)  # typed during the cycle: waits in the UART buffer
        self.at(handled + 0.05, pong)
        if self.commands_extend and self.sleep_at is not None:
            self.sleep_at = max(self.sleep_at, handled + 0.05 + self.idle_s)
