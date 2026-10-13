---
name: pio-flash
description: Upload (flash) the firmware to a connected ESP32 board over USB and confirm it boots. Use when the user asks to flash, upload, deploy to the board, or test on hardware.
disable-model-invocation: true
---

# Flash the ESP32

Flashing touches real hardware, so only do it when the user asks (this skill is user-invoked via `/pio-flash`).

## Steps

1. Find the board's serial port:
   ```bash
   ~/.platformio/penv/Scripts/pio.exe device list
   ```
   Look for a COM port with a CP210x / CH340 / USB-Serial description. If none appear, tell the user to check the USB cable (must be a data cable) and driver.
2. Build and upload (PlatformIO auto-detects the port if there is only one):
   ```bash
   ~/.platformio/penv/Scripts/pio.exe run -e esp32dev -t upload 2>&1 | tail -30
   ```
   Add `--upload-port COMx` if several ports are present.
3. Confirm the boot by capturing serial output — follow the `pio-monitor` skill.

## Troubleshooting

- `Failed to connect to ESP32: Timed out waiting for packet header` → hold the **BOOT** button while upload starts, or lower `upload_speed` to 460800 in platformio.ini.
- `could not open port ... Access is denied` → something else (Serial Monitor in VS Code, Arduino IDE) holds the port. Ask the user to close it.
