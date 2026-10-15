"""Board-test harness: talks to the real firmware over USB serial (ADR 0001).

Tests depend only on tagged lines, `@tag key=value ...`. Every other line
(log_* output, human-readable text, boot ROM noise) is ignored.
"""

import os
import re
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
