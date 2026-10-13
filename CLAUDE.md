# ESPSprint

ESP32 firmware (ESP32 DevKit, `esp32dev`) built with PlatformIO + Arduino framework. Windows dev machine.

## Tooling

- PlatformIO CLI is **not on PATH**: use `~/.platformio/penv/Scripts/pio.exe` (Python: `~/.platformio/penv/Scripts/python.exe`).
- Build: `pio run -e esp32dev` · Release: `pio run -e esp32dev-release` · Flash: `pio run -t upload` · Tests: `pio test -e esp32dev`
- Project skills in `.claude/skills/`: `pio-build`, `pio-flash` (user-invoked only), `pio-monitor`, `pio-add-lib`, `pio-test`, `esp32-module`.

## Layout

- `src/main.cpp` — `setup()`/`loop()` only; wires modules together.
- `include/config.h` — all pin numbers and tunables (`config::` namespace).
- `lib/<Module>/src/` — one folder per feature/hardware component.
- `test/test_<module>/` — Unity tests.
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
