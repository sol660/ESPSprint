"""Stand-ins for a pyserial port, for the harness self-check.

Model of the board's wiring: RTS high holds EN low (board in reset, silent);
RTS going low lets it boot. Lines already queued are "old" serial input.
"""

import re


class FakeSerial:
    """A port with nothing on it, apart from any stale lines queued before the reset."""

    def __init__(self, stale_lines=()):
        self._rx = [line.encode() + b"\r\n" for line in stale_lines]
        self._rts = False
        self.dtr = False
        self.written = b""

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, value):
        released = self._rts and not value
        self._rts = value
        if released:
            self.on_boot()

    def on_boot(self):
        pass  # silent: the board never says anything

    def emit(self, line):
        self._rx.append(line.encode() + b"\r\n")

    def reset_input_buffer(self):
        self._rx.clear()

    @property
    def in_waiting(self):
        return sum(len(b) for b in self._rx)

    def readline(self):
        return self._rx.pop(0) if self._rx else b""  # b"" = read timeout

    def write(self, data):
        self.written += data
        self.on_write(data)
        return len(data)

    def on_write(self, data):
        pass

    def flush(self):
        pass

    def close(self):
        pass


class FakeFirmware(FakeSerial):
    """Behaves like the real firmware: a boot banner after reset, and ping -> pong."""

    def __init__(self, fw, **kw):
        super().__init__(**kw)
        self.fw = fw

    def on_boot(self):
        self.emit("ets Jun  8 2016 00:22:57")
        self.emit(f"@boot fw={self.fw} boot=1 cause=PowerOn")
        self.emit("[   512][E][main.cpp:60] setup(): MPU6050 init failed")

    def on_write(self, data):
        m = re.match(rb"ping (\S+)\n", data)
        if m:
            self.emit("@pong " + m.group(1).decode())


class FirmwareThatDoesNotReset(FakeFirmware):
    """Live firmware whose reset line does nothing (e.g. auto-reset circuit missing):
    it answers pings but never prints a fresh boot banner."""

    def on_boot(self):
        pass
