---
name: pio-monitor
description: Capture and read serial output from the running ESP32 for a fixed time window. Use when the user asks what the board is printing, to check boot logs, debug a crash / Guru Meditation / reboot loop, or verify behaviour after flashing.
---

# Read serial output

`pio device monitor` runs forever and is interactive, so never run it bare. Use the bundled script, which captures for a fixed number of seconds and exits:

```bash
~/.platformio/penv/Scripts/python.exe .claude/skills/pio-monitor/capture.py --seconds 10
```

Options: `--port COM5` (auto-detected otherwise), `--baud 115200`, `--reset` (pulse EN to reboot the board first so you see the boot log).

## Reading the output

- `Guru Meditation Error` / `Backtrace: 0x...` → crash. Decode the backtrace addresses to file:line:
  ```bash
  ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-addr2line.exe -pfiaC -e .pio/build/esp32dev/firmware.elf 0x400d1234 0x400d5678 ...
  ```
- `rst:0xc (SW_CPU_RESET)` repeating → reboot loop, usually a crash in `setup()`.
- `Brownout detector was triggered` → power problem, not code. Tell the user.
- Garbage characters → baud mismatch with `monitor_speed`.

For interactive use, the user can run `pio device monitor` themselves in a terminal (it decodes backtraces automatically via the `esp32_exception_decoder` filter).
