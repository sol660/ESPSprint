"""Board-test harness: talks to the real firmware over USB serial (ADR 0001).

Tests depend only on tagged lines, `@tag key=value ...`. Every other line
(log_* output, human-readable text, boot ROM noise) is ignored.
"""

import os
import random
import re
import textwrap
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BAUD = 115200
READ_TIMEOUT_S = 0.2  # one readline() wait; expect() loops until its own deadline


def firmware_version():
    """config::FW_VERSION from include/config.h, the version the board should report."""
    text = (ROOT / "include" / "config.h").read_text(encoding="utf-8")
    m = re.search(r'FW_VERSION\s*=\s*"([^"]+)"', text)
    assert m, "FW_VERSION not found in include/config.h"
    return m.group(1)


def config_number(name, path=None):
    """A whole-number `constexpr <type> NAME = <expr>;` from include/config.h.

    <expr> may use digits, spaces and * (e.g. `30 * 1000`). Names set inside the
    ESPSPRINT_HIL_TIMINGS #ifdef appear twice and are refused: use the test's
    own copy of those.
    """
    text = Path(path or ROOT / "include" / "config.h").read_text(encoding="utf-8")
    found = re.findall(r"constexpr\s+\w+\s+%s\s*=\s*([0-9 *]+?)\s*;" % re.escape(name), text)
    assert len(found) == 1, f"{name}: expected one whole-number constexpr in config.h, found {len(found)}"
    value = 1
    for factor in found[0].split("*"):
        value *= int(factor)
    return value


def open_port(device):
    """Opens the board's port without pulsing reset (DTR/RTS low before open)."""
    import serial
    port = serial.Serial()
    port.port = device
    port.baudrate = BAUD
    port.timeout = READ_TIMEOUT_S
    port.dtr = False
    port.rts = False
    try:
        port.open()
    except serial.SerialException as e:
        pytest.fail(f"could not open {device}: {e} (is a serial monitor still open?)")
    return port


class Board:
    """The plugged-in board, seen through its serial port (or a fake one)."""

    def __init__(self, port, clock=time.monotonic, sleep=time.sleep):
        self.port = port
        self._clock = clock
        self._sleep = sleep
        self.recent = deque(maxlen=20)  # last lines seen, for failure messages

    def reset(self):
        """Resets the board through RTS (EN) with DTR (IO0) high, so it boots the firmware.

        Old serial input is cleared while the board is held in reset, so only
        output from this boot can satisfy a test.
        """
        self.port.dtr = False  # IO0 high: normal boot, not the bootloader
        self.port.rts = True   # EN low: board held in reset, silent
        self._sleep(0.1)
        self.port.reset_input_buffer()
        while self.port.in_waiting:
            self.port.readline()
        self.recent.clear()
        self.port.rts = False  # EN high: board boots

    def send(self, line):
        self.port.write(line.encode() + b"\n")
        self.port.flush()

    def expect(self, tag, timeout_s, match=None, what=None):
        """Reads lines until a `@tag` line (accepted by `match`) arrives; fails after timeout_s."""
        deadline = self._clock() + timeout_s
        while self._clock() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            self.recent.append(line)
            t = parse_tag(line)
            if t and t.tag == tag and (match is None or match(t)):
                return t
        seen = "\n".join(f"  | {line}" for line in self.recent) or "  (nothing at all)"
        pytest.fail(f"no {what or '@' + tag} within {timeout_s} s. Last lines seen:\n{seen}")

    def now(self):
        """The harness clock in seconds, for timing what the board does."""
        return self._clock()

    def watch(self, duration_s):
        """Reads for duration_s and returns every tagged line seen, in order."""
        deadline = self._clock() + duration_s
        seen = []
        while self._clock() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            self.recent.append(line)
            t = parse_tag(line)
            if t:
                seen.append(t)
        return seen

    def sync(self, timeout_s):
        """Pings with a fresh random number and waits for its pong.

        Commands are handled one at a time, so every tagged line read after this
        was printed after this point: earlier output can't answer a later command.
        """
        n = random_id()
        self.send(f"ping {n}")
        self.expect("pong", timeout_s, match=lambda t: t.args == [n], what=f"@pong {n}")

    def ask(self, command, tag, timeout_s):
        """Sends `command` after a sync, and returns its `@tag` reply."""
        self.sync(timeout_s)
        self.send(command)
        return self.expect(tag, timeout_s, what=f"@{tag} in reply to `{command}`")


def random_id():
    """A random number unique to this run, for replies that must echo it."""
    return str(random.SystemRandom().randrange(10**8, 10**9))


class Installer:
    """The person at the bench during a manual test (spec #14, story 14).

    Each step prints clear instructions, then waits for Enter.
    """

    def __init__(self, ask=input, say=print):
        self._ask = ask
        self._say = say

    def do(self, instructions):
        self._say("\n=== MANUAL STEP ===\n" + textwrap.dedent(instructions).strip())
        try:
            self._ask("Press Enter when done (Ctrl+C aborts)... ")
        except EOFError:
            pytest.fail("manual test needs a terminal to wait for Enter, but stdin is closed")

# USB-serial chips found on ESP32 DevKit boards: (vid, pid) -> name.
ESP_USB_CHIPS = {
    (0x10C4, 0xEA60): "CP210x",
    (0x1A86, 0x7523): "CH340",
    (0x1A86, 0x55D4): "CH9102",
    (0x0403, 0x6001): "FT232R",
}


def find_port(ports=None, env=None):
    """The board's serial port: $ESP_PORT if set, else the one port with an ESP32 USB chip.

    Fails (never skips) when there is no board, listing every port it checked.
    """
    env = os.environ if env is None else env
    if env.get("ESP_PORT"):
        return env["ESP_PORT"]
    if ports is None:
        from serial.tools import list_ports
        ports = list_ports.comports()
    ports = list(ports)
    boards = [p for p in ports if (p.vid, p.pid) in ESP_USB_CHIPS]
    if len(boards) == 1:
        return boards[0].device
    if boards:
        pytest.fail("more than one ESP32 board found (" +
                    ", ".join(p.device for p in boards) + "): choose one with ESP_PORT")
    if not ports:
        pytest.fail("no board found: no serial ports at all. Is the board plugged in, "
                    "and is the USB-serial driver (CP210x/CH340) installed?")
    checked = "\n".join(
        f"  {p.device}: {p.description}"
        + (f" (USB {p.vid:04X}:{p.pid:04X})" if p.vid is not None else "")
        for p in ports)
    pytest.fail("no board found. Ports checked (none has an ESP32 USB-serial chip "
                "CP210x/CH340/CH9102/FT232R):\n" + checked +
                "\nCheck the cable and driver, or set ESP_PORT.")


@dataclass
class Tagged:
    tag: str
    fields: dict = field(default_factory=dict)  # key=value pairs
    args: list = field(default_factory=list)    # bare values, in order
    line: str = ""

    def number(self, key):
        """The value of `key` as a float; fails the test if it's missing or not a number."""
        if key not in self.fields:
            pytest.fail(f"no {key}= in: {self.line}")
        try:
            return float(self.fields[key])
        except ValueError:
            pytest.fail(f"{key}= is not a number in: {self.line}")


def parse_tag(line):
    """The tagged line as a Tagged, or None for any other line."""
    line = line.strip()
    if not line.startswith("@") or len(line) < 2:
        return None
    tag, *tokens = line[1:].split()
    t = Tagged(tag=tag, line=line)
    for tok in tokens:
        key, sep, value = tok.partition("=")
        if sep:
            t.fields[key] = value
        else:
            t.args.append(tok)
    return t
