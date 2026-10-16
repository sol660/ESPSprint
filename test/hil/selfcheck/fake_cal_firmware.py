"""A fake firmware that answers `cal`, `read` and `baseline` like the real one (#22).

It models a sensor with per-axis offset and scale errors, so a calibration
worked out from its six faces really does correct it. Attributes break it in
one specific way for each self-check.
"""

import math

from fake_serial import FakeFirmware

# The modelled sensor: raw = OFFSET + SCALE * true gravity (g). Flat, it reads
# about 0.82 g uncorrected, like the real module's 0.876 g.
OFFSET = (0.02, -0.03, -0.06)
SCALE = (1.00, 0.99, 0.88)
GRAVITY = {"+x": (1, 0, 0), "-x": (-1, 0, 0), "+y": (0, 1, 0), "-y": (0, -1, 0),
           "+z": (0, 0, 1), "-z": (0, 0, -1)}
FLAT = (1.2, -0.7)  # true (pitch, roll) of the bench when flat


class CalBoard(FakeFirmware):
    """`up` is the face pointing up (the fake installer sets it). Flat is `+z`."""

    def __init__(self, fw, capture_ok=True, faces_stuck=False, wrong_step=False, save_ok=True,
                 erases_baseline=True, applies_cal=True, claims_cal=True, g_error=0.0,
                 baseline_ok=True, subtracts_baseline=True, stable=1, **kw):
        super().__init__(fw, **kw)
        self.up = "+z"
        self.captured = {}
        self.cal = None
        self.baseline = None
        self.capture_ok = capture_ok
        self.faces_stuck = faces_stuck    # always reports faces=1
        self.wrong_step = wrong_step      # replies for another face
        self.save_ok = save_ok
        self.erases_baseline = erases_baseline
        self.applies_cal = applies_cal    # corrects readings once saved
        self.claims_cal = claims_cal      # says cal=1 once saved, corrected or not
        self.g_error = g_error            # added to the reported g
        self.baseline_ok = baseline_ok
        self.subtracts_baseline = subtracts_baseline
        self.stable = stable

    def _raw(self):
        return [OFFSET[k] + SCALE[k] * GRAVITY[self.up][k] for k in range(3)]

    def _reading(self):
        a = self._raw()
        corrected = self.cal is not None and self.applies_cal
        if corrected:
            off, sc = self.cal
            a = [(a[k] - off[k]) / sc[k] for k in range(3)]
        g = math.sqrt(sum(v * v for v in a)) + self.g_error
        bp, br = self.baseline if (self.baseline and self.subtracts_baseline) else (0.0, 0.0)
        p, r = FLAT[0] - bp, FLAT[1] - br
        cal = 1 if (self.cal is not None and self.claims_cal) else 0
        return (f"pitch={p:.3f} roll={r:.3f} tilt={math.hypot(p, r):.3f} stable={self.stable} "
                f"noise=0.00110 temp=24.0 g={g:.4f} cal={cal}")

    def _count(self):
        return 1 if self.faces_stuck else len(self.captured)

    def on_write(self, data):
        super().on_write(data)  # ping -> pong
        cmd = data.decode().strip()
        if cmd.startswith("cal "):
            self._cal(cmd[4:])
        elif cmd == "read":
            self.emit(f"@read ok=1 {self._reading()}")
        elif cmd == "baseline":
            if self.baseline_ok:
                self.baseline = FLAT
            base = (f"baseline=1 bpitch={FLAT[0]:.3f} broll={FLAT[1]:.3f}"
                    if self.baseline else "baseline=0")
            self.emit(f"@status ok={1 if self.baseline_ok else 0} {base} {self._reading()}")

    def _cal(self, arg):
        if arg in GRAVITY:
            step = "+x" if (self.wrong_step and arg != "+x") else arg
            if not self.capture_ok:
                self.emit(f"@cal ok=0 step={step} faces={self._count()} err=moving")
                return
            self.captured[arg] = self._raw()
            a = self.captured[arg]
            self.emit(f"@cal ok=1 step={step} faces={self._count()} "
                      f"ax={a[0]:.4f} ay={a[1]:.4f} az={a[2]:.4f}")
        elif arg == "save":
            if not self.save_ok or len(self.captured) < 6:
                self.emit(f"@cal ok=0 step=save faces={len(self.captured)} err=missing_faces")
                return
            axes = "xyz"
            off = [(self.captured["+" + axes[k]][k] + self.captured["-" + axes[k]][k]) / 2
                   for k in range(3)]
            sc = [(self.captured["+" + axes[k]][k] - self.captured["-" + axes[k]][k]) / 2
                  for k in range(3)]
            self.cal = (off, sc)
            self.captured = {}
            if self.erases_baseline:
                self.baseline = None
            tail = " baseline=0" if self.erases_baseline else ""
            self.emit("@cal ok=1 step=save faces=0 "
                      + " ".join(f"o{a}={o:.4f}" for a, o in zip(axes, off)) + " "
                      + " ".join(f"s{a}={s:.4f}" for a, s in zip(axes, sc)) + tail)
