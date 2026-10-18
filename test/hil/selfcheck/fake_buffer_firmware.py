"""A fake firmware with an offline buffer (#7): the wake's `@upload` and `@buffer`
lines at boot, `status` (`@status` then `@buffer`) and `clearbuf`.

Each fault knob makes it broken in one specific way, for the self-check.
"""

from fake_serial import FakeFirmware

STATUS = ("@status ok=1 baseline=1 bpitch=0.000 broll=0.000 pitch=0.010 roll=-0.020 "
          "tilt=0.022 stable=1 noise=0.00110 temp=24.3 g=1.0020 cal=0")


class BufferBoard(FakeFirmware):
    """`count` readings in the buffer. The boot wake's upload works when `online`.

    Faults:
      wake_line: False -> no @buffer after the wake's @upload; or a str to print instead
      status_line: False -> no @buffer after @status; or a str to print instead
      knows_clearbuf: False -> `clearbuf` is an unknown command (no reply)
      clears: False -> clearbuf reports success but the buffer keeps its readings
      clear_ok: False -> clearbuf reports a flash error (ok=0) and clears nothing
      reported_cleared / reported_count: what the @clearbuf line says, if not the truth
    """

    def __init__(self, fw, count=3, online=True, wake_line=True, status_line=True,
                 knows_clearbuf=True, clears=True, clear_ok=True, reported_cleared=None,
                 reported_count=None, **kw):
        super().__init__(fw, **kw)
        self.count = count
        self.online = online
        self.wake_line = wake_line
        self.status_line = status_line
        self.knows_clearbuf = knows_clearbuf
        self.clears = clears
        self.clear_ok = clear_ok
        self.reported_cleared = reported_cleared
        self.reported_count = reported_count

    def _line(self, setting, healthy):
        if setting is True:
            self.emit(healthy)
        elif setting:
            self.emit(setting)

    def on_boot(self):
        super().on_boot()
        if self.online:
            sent, self.count = self.count + 1, 0  # the backlog, then this wake's reading
        else:
            sent, self.count = 0, self.count + 1
        self.emit(f"@upload ok={1 if self.online else 0} tag=none seq=1 boot=1")
        self._line(self.wake_line, f"@buffer count={self.count} sent={sent} dropped=0 skipped=0")

    def on_write(self, data):
        super().on_write(data)  # ping -> pong
        cmd = data.decode().strip()
        if cmd == "status":
            self.emit("offline buffer: ... (human-readable line, ignored by tests)")
            self.emit(STATUS)
            self._line(self.status_line, f"@buffer count={self.count} sent=0 dropped=0 skipped=0")
        elif cmd == "clearbuf" and self.knows_clearbuf:
            before = self.count
            if self.clear_ok and self.clears:
                self.count = 0
            cleared = before - self.count if self.clear_ok else 0
            if self.reported_cleared is not None:
                cleared = self.reported_cleared
            shown = self.count if self.reported_count is None else self.reported_count
            self.emit(f"@clearbuf ok={1 if self.clear_ok else 0} cleared={cleared} count={shown}")
