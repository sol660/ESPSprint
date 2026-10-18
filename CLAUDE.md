# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# ESPSprint

ESP32 firmware (ESP32 DevKit, `esp32dev`) built with PlatformIO + Arduino framework. Windows dev machine.

The product is a remote landslide/tilt monitor (school prototype). The full spec is GitHub issue #1 on `sol660/ESPSprint`; read it before designing a feature. The PCB pinout is fixed, so don't propose hardware changes.

## Tooling

- PlatformIO CLI is **not on PATH**: use `~/.platformio/penv/Scripts/pio.exe` (Python: `~/.platformio/penv/Scripts/python.exe`).
- Build: `pio run -e esp32dev` · Flash: `pio run -t upload` · Monitor: `pio device monitor` (115200 baud)
- `esp32dev_hil`: the same firmware with short sleep/window timings (`ESPSPRINT_HIL_TIMINGS` in `config.h`), needed by `test/hil/board/test_sleep.py`. Flash it with `pio run -e esp32dev_hil -t upload`, then flash `esp32dev` again when done.
- PC logic tests: `pio test -e native` (one suite: `-f native/test_<name>`). On-board hardware tests: `pio test -e esp32dev` (flashes the board).
- Board tests (pytest, from the PC, ADR 0001): run `~/.platformio/penv/Scripts/python.exe scripts/setup_venv.py` once to set up `.venv`. Then the board suite is `.venv/Scripts/python.exe -m pytest test/hil/board` (port is found automatically, or set `ESP_PORT`; `--manual` also runs the hand tests), and the no-board harness self-check is `.venv/Scripts/python.exe -m pytest test/hil/selfcheck`. To run one file, give its path, for example `test/hil/board/test_reading.py`. Each board test file has a matching `test/hil/selfcheck/test_*_selfcheck.py` that runs it against a fake firmware (`fake_*_firmware.py`) and checks that the test fails when the board is silent or broken (ADR 0001). When you add a board test, add its self-check too. Firmware replies that tests check are tagged lines: `@tag key=value ...`, formatted in `lib/WakeCycle/src/TaggedReply.*`. Only add keys; never rename or remove them, because the board tests parse them.
- Dashboard tests: `node --test dashboard/test/*.test.js`. Node isn't installed on the dev PC, so use VS Code's: `ELECTRON_RUN_AS_NODE=1 "$HOME/AppData/Local/Programs/Microsoft VS Code/Code.exe" --test dashboard/test/*.test.js`.
- CI (`.github/workflows/ci.yml`, Ubuntu) runs the native tests, the harness self-check, the dashboard tests, and builds `esp32dev` and `esp32dev_hil`. It copies `secrets.example.h` to `secrets.h`, so code must build with the template values.
- The `native` env needs a complete MinGW gcc. The `gcc.exe` in `~/bin` (first on PATH) has no headers, so `scripts/native_toolchain.py` puts `~/tdm-gcc/bin` first (override with `NATIVE_GCC_BIN`).
- Project skills in `.claude/skills/`: `pio-build`, `pio-flash` (user-invoked only), `pio-monitor`, `pio-add-lib`, `pio-test`, `esp32-module`.

## Layout

- Firmware is one **wake cycle** per boot. `lib/WakeCycle/` holds `runWakeCycle()`, which has no hardware access: it gets the wake cause, `RtcState` (kept across deep sleep), `CycleConfig`, and six ports (`Ports.h`: Imu, Clock, Store, Uplink, CamTrigger, Sleeper). It returns a `WakeResult`. All decision logic belongs here, and it must not include Arduino headers. Installer serial commands (`read`, `baseline`, `status`) go through the second entry point, `runCommand()`, and the accelerometer calibration (`cal <face|save|clear>`) through `runCalCommand()`; `ping <n>` (board tests) is answered in `main.cpp`. The network commands (`wifi [tag]`, `alert [n]`) run as the `WifiCommand` state machine, which `loop()` polls with `millis()` timing so it never blocks. `CommandWindow` decides when the board sleeps. `main.cpp`'s `loop()` only parses commands, polls, and prints the result.
- Payload formatting is also hardware-free in `lib/WakeCycle/`: `ReadingJson.*` (a reading as a Supabase `readings` row) and `AlertMessage.*` (ntfy alert text). The uplink and the native tests share them, so change wire formats there, not in `Esp32Ports`.
- `lib/Esp32Ports/` — the real port implementations. `src/main.cpp` builds them, runs one cycle and sleeps as the result says. The offline reading buffer is a ring file on LittleFS (`Esp32Store`); its layout has a version number, so bump `BUFFER_LAYOUT_VERSION` when `Reading` changes. Everything else in the Store is NVS (`Preferences`).
- `include/config.h` — all pins and tunables (`config::`), Arduino-free. Secrets go in the git-ignored `include/secrets.h`; copy it from `include/secrets.example.h`. The pytest board tests also read `SUPABASE_URL`, `SUPABASE_KEY`, `DEVICE_TOKEN`, `DEVICE_ID` and `NTFY_TOPIC` out of `secrets.h` as text, so keep each of those on one line.
- `supabase/` — `schema.sql` (readings table + access rules) and `SETUP.md` (first-time setup checklist). Inserts need the `x-device-token` header (`DEVICE_TOKEN`), checked against a hash in `private.device_tokens`; the public key alone can only read. Schema changes must be safe to re-run, and the user runs them in the live project.
- `dashboard/` — a static page (GitHub Pages, public repo) that reads the `real_readings` view with the public key, and reloads every 5 min. `config.js` is git-ignored (template `config.example.js`); a test checks its WATCH/ALERT lines against `config.h`. It's published by `.github/workflows/dashboard-pages.yml`, which only runs when started by hand, so after dashboard changes the user runs it again.
- `test/native/test_<name>/` — PC tests at the wake-cycle seam, using the fakes in `test/native/fakes/Fakes.h`. `test/embedded/test_<name>/` — on-board tests of the real ports.
- `docs/improvements.md` — log of changes, decisions, open questions and items still to verify on the board. Add to it when you finish a ticket.
- `.pio/` — generated build output; never edit.

## Rules

- `loop()` and module `update()` must be non-blocking: use `millis()` timing, not `delay()`.
- No magic pin numbers outside `config.h`.
- Use `log_e/w/i/d/v` for diagnostics; `Serial.printf` only for intentional user-facing output.
- Pin every library in `lib_deps` with `owner/name@^version`.
- Check the code compiles (`pio-build`) after every change. Don't flash the board unless asked.
- The HTTPS posts (mbedTLS certificate check) run on the Arduino loop task, which has `config::LOOP_TASK_STACK_BYTES` (16 KB). Keep large buffers off the stack on that path. PC tests can't catch a stack overflow, so after changing the upload or reading path, run the board suite (with the user's OK to flash).

## Agent skills

### Issue tracker

Issues live in GitHub Issues on `sol660/ESPSprint` (via `gh`). See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
