# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# ESPSprint

ESP32 firmware (ESP32 DevKit, `esp32dev`) built with PlatformIO + Arduino framework. Windows dev machine.

The product is a remote landslide/tilt monitor (school prototype). The full spec is GitHub issue #1 on `sol660/ESPSprint`; read it before designing a feature. The PCB pinout is fixed, so don't propose hardware changes.

## Tooling

- PlatformIO CLI is **not on PATH**: use `~/.platformio/penv/Scripts/pio.exe` (Python: `~/.platformio/penv/Scripts/python.exe`).
- Build: `pio run -e esp32dev` · Flash: `pio run -t upload` · Monitor: `pio device monitor` (115200 baud)
- `esp32dev_hil`: the same firmware with short sleep/window timings (`ESPSPRINT_HIL_TIMINGS` in `config.h`), needed by `test/hil/board/test_sleep.py`.
- PC logic tests: `pio test -e native` (one suite: `-f native/test_<name>`). On-board hardware tests: `pio test -e esp32dev` (flashes the board).
- Board tests (pytest, from the PC, ADR 0001): run `~/.platformio/penv/Scripts/python.exe scripts/setup_venv.py` once to set up `.venv`. Then the board suite is `.venv/Scripts/python.exe -m pytest test/hil/board` (port is found automatically, or set `ESP_PORT`; `--manual` also runs the hand tests), and the no-board harness self-check is `.venv/Scripts/python.exe -m pytest test/hil/selfcheck`. Firmware replies that tests check are tagged lines: `@tag key=value ...`.
- The `native` env needs a complete MinGW gcc. The `gcc.exe` in `~/bin` (first on PATH) has no headers, so `scripts/native_toolchain.py` puts `~/tdm-gcc/bin` first (override with `NATIVE_GCC_BIN`).
- Project skills in `.claude/skills/`: `pio-build`, `pio-flash` (user-invoked only), `pio-monitor`, `pio-add-lib`, `pio-test`, `esp32-module`.

## Layout

- Firmware is one **wake cycle** per boot. `lib/WakeCycle/` holds `runWakeCycle()`, which has no hardware access: it gets the wake cause, `RtcState` (kept across deep sleep), `CycleConfig`, and six ports (`Ports.h`: Imu, Clock, Store, Uplink, CamTrigger, Sleeper). It returns a `WakeResult`. All decision logic belongs here, and it must not include Arduino headers. Installer serial commands (`read`, `baseline`, `status`, `wifi`) go through the second entry point, `runCommand()`. `main.cpp`'s `loop()` only parses the command and prints the result.
- `lib/Esp32Ports/` — the real port implementations. `src/main.cpp` builds them, runs one cycle and sleeps as the result says.
- `include/config.h` — all pins and tunables (`config::`), Arduino-free. Secrets go in the git-ignored `include/secrets.h`; copy it from `include/secrets.example.h`.
- `supabase/` — `schema.sql` (readings table + access rules) and `SETUP.md` (first-time setup checklist).
- `test/native/test_<name>/` — PC tests at the wake-cycle seam, using the fakes in `test/native/fakes/Fakes.h`. `test/embedded/test_<name>/` — on-board tests of the real ports.
- `docs/improvements.md` — log of changes, decisions, open questions and items still to verify on the board. Add to it when you finish a ticket.
- `.pio/` — generated build output; never edit.

## Rules

- `loop()` and module `update()` must be non-blocking: use `millis()` timing, not `delay()`.
- No magic pin numbers outside `config.h`.
- Use `log_e/w/i/d/v` for diagnostics; `Serial.printf` only for intentional user-facing output.
- Pin every library in `lib_deps` with `owner/name@^version`.
- Check the code compiles (`pio-build`) after every change. Don't flash the board unless asked.

## Agent skills

### Issue tracker

Issues live in GitHub Issues on `sol660/ESPSprint` (via `gh`). See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
