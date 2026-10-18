"""A fake firmware that answers `read`, `baseline` and `status` like the real one (#17).

Its tagged replies are built from attributes the self-check sets, so a check can
make the board look healthy, or broken in one specific way.
"""

from fake_serial import FakeFirmware


class FakeClock:
    """Each look at the clock moves it on 50 ms, so timeouts pass instantly."""

    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 0.05
        return self.t


class StillBoard(FakeFirmware):
    """A board resting still. `raw` is its true (pitch, roll); the delta is raw minus baseline."""

    def __init__(self, fw, raw=(1.2, -0.7), g=1.002, noise=0.0011, stable=1, temp=24.3,
                 jitter=0.0, subtracts_baseline=True, baseline_ok=True, **kw):
        super().__init__(fw, **kw)
        self.raw = raw
        self.g = g
        self.noise = noise
        self.stable = stable
        self.temp = temp
        self.jitter = jitter  # alternate reads are offset by +/- jitter/2 degrees
        self.subtracts_baseline = subtracts_baseline
        self.baseline_ok = baseline_ok
        self.baseline = None
        self.reads = 0

    def _delta(self):
        bp, br = self.baseline if (self.baseline and self.subtracts_baseline) else (0.0, 0.0)
        wobble = self.jitter / 2 * (1 if self.reads % 2 else -1)
        return self.raw[0] - bp + wobble, self.raw[1] - br + wobble

    def _reading_keys(self):
        p, r = self._delta()
        tilt = (p * p + r * r) ** 0.5
        return (f"pitch={p:.3f} roll={r:.3f} tilt={tilt:.3f} stable={self.stable} "
                f"noise={self.noise:.5f} temp={self.temp:.1f} g={self.g:.4f}")

    def _status(self, ok):
        base = (f"baseline=1 bpitch={self.baseline[0]:.3f} broll={self.baseline[1]:.3f}"
                if self.baseline else "baseline=0")
        self.emit(f"@status ok={ok} {base} {self._reading_keys()}")

    def on_write(self, data):
        super().on_write(data)  # ping -> pong
        cmd = data.decode().strip()
        if cmd == "read":
            self.reads += 1
            self.emit("tilt delta pitch ... (human-readable line, ignored by tests)")
            self.emit(f"@read ok=1 {self._reading_keys()}")
        elif cmd == "baseline":
            self.emit("capturing baseline: keep the unit still...")
            if self.baseline_ok:
                self.baseline = self.raw
            self._status(1 if self.baseline_ok else 0)
        elif cmd == "status":
            self._status(1)


class BoardThatPrintsAReadingAtBoot(FakeFirmware):
    """Prints a healthy-looking @read once at boot but never answers commands other than ping."""

    def on_boot(self):
        super().on_boot()
        self.emit("@read ok=1 pitch=0.000 roll=0.000 tilt=0.000 stable=1 noise=0.00100 "
                  "temp=24.0 g=1.0000")
        self.emit("@status ok=1 baseline=1 bpitch=0.000 broll=0.000 pitch=0.000 roll=0.000 "
                  "tilt=0.000 stable=1 noise=0.00100 temp=24.0 g=1.0000")


class FakeInstaller:
    """Stands in for the person at the bench: each step runs the next scripted action."""

    def __init__(self, *actions):
        self.actions = list(actions)
        self.steps = []

    def do(self, instructions):
        self.steps.append(instructions)
        if self.actions:
            self.actions.pop(0)()
