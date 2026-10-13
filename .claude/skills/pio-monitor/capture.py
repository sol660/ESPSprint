"""Capture ESP32 serial output for a fixed time, then exit.

Run with PlatformIO's bundled Python, which ships pyserial:
    ~/.platformio/penv/Scripts/python.exe capture.py --seconds 10 [--port COM5] [--reset]
"""
import argparse
import sys
import time

import serial
from serial.tools import list_ports

USB_HINTS = ("CP210", "CH340", "CH910", "USB-SERIAL", "USB SERIAL", "UART", "FTDI")


def guess_port():
    ports = list(list_ports.comports())
    for p in ports:
        desc = f"{p.description} {p.hwid}".upper()
        if any(h in desc for h in USB_HINTS):
            return p.device
    if len(ports) == 1:
        return ports[0].device
    sys.exit("No ESP32 serial port found. Ports: " + (", ".join(p.device for p in ports) or "none"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--reset", action="store_true", help="pulse EN to reboot the board first")
    args = ap.parse_args()

    port = args.port or guess_port()
    print(f"--- capturing {port} @ {args.baud} for {args.seconds}s ---", flush=True)
    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        if args.reset:
            # DTR high / RTS low = run mode; toggling RTS pulses EN
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
        end = time.time() + args.seconds
        while time.time() < end:
            data = ser.read(ser.in_waiting or 1)
            if data:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()
    print("\n--- capture finished ---")


if __name__ == "__main__":
    main()
