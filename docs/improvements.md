# Improvements and changes

A running log of what changed, the decisions made along the way, and what's still open. Newest first. The spec is GitHub issue #1, and each ticket is its own issue.

## Open questions

These need a decision from the installer.

- **WiFi network count.** Spec #1 says both "up to 2 networks tried" and lists three networks (home, school, phone hotspot). `config::WIFI_MAX_NETWORKS = 2`, but `include/secrets.example.h` lists 3, so the hotspot would never be tried. Should it be 3?
- **Timezone.** `config::TIMEZONE` is `"UTC0"`, so the photo slots (07:00, 12:00 …) would fire at UTC times. Set it to the site's POSIX TZ string.
- **Values the spec doesn't set** (placeholders in `include/config.h`, marked "Not set by spec"):

  | Constant | Value |
  |---|---|
  | `SHOCK_PHOTO_MIN_GAP_S` | 30 min |
  | `CAM_PULSE_MS` | 50 ms |
  | `SERIAL_SESSION_IDLE_MS` | 60 s |
  | `WINDOW_SAMPLES` | 50 |
  | `BASELINE_WINDOWS` | 20 |
  | `BASELINE_TIME_LIMIT_MS` | 25 s |
  | `IMU_SAMPLE_RATE_HZ` | 100 Hz |

## Still to verify on the board

- [ ] `pio test -e esp32dev`, with the MPU6050 wired and the board resting flat. This runs `embedded/test_imu` and `embedded/test_store_reboot`. Both compile but haven't run yet.
- [ ] #3 manual check: flash, type `read`, tilt the board by hand, then put it back. The delta should move in the expected direction and return to about 0°. Also check which way is "positive" pitch and roll on the real mounting.

## 2026-10-14: #3 Tilt reading and baseline over serial (not yet committed)

**Built test-first** at seam 1 (wake cycle on the PC). Every test went red before it went green, except two regression guards, noted below.

- Measurement: samples are split into windows, and each window is gated on accelerometer std dev and gyro std dev. The accepted windows are averaged, and pitch and roll come from the averaged direction. If the time limit runs out first, the reading is `stable=false`.
- Tilt delta = reading − stored baseline.
- Every reading carries a noise figure: the mean accelerometer std dev of the accepted windows, in g.
- On first boot with no baseline, one is captured as a long average and saved. It isn't saved if the board was moving.
- Serial commands through a new core function, `runCommand()`:
  - `read`: tilt delta against the stored baseline
  - `baseline`: re-capture and save
  - `status`: the baseline and the last reading, which is kept in `RtcState`
- Tests: `test/native/test_tilt_reading` (8 tests) and `test/native/test_serial_commands` (3 tests). Two of the tilt tests passed as soon as they were written, because the code came first: "noisy windows left out of the average" and "no baseline saved while shaking". They stay as regression guards.

**Real hardware (seam 2):**

- `Esp32Imu`: an MPU6050 driver over plain `Wire`, with no extra library. It sets ±2 g, the lowest DLPF setting and 100 Hz, and paces `readSample()` to that rate.
- `Esp32Store`: saves the baseline in flash through `Preferences` (NVS). The namespace can be set, so tests never touch the real baseline.
- On-board tests: `test/embedded/test_imu` and `test/embedded/test_store_reboot`. The second one does a real `esp_restart()` and checks the baseline on the next boot. Each failure message names the likely hardware cause.
- `src/main.cpp`: starts the IMU, prints the wake reading, and handles serial commands in `loop()` using a fixed buffer, so there's no heap use.

**Decisions:**

- **Gyro gate uses the spread, not the level.** An uncalibrated MPU6050 gyro typically reads several °/s even when still, so gating on the absolute rate would reject every window on the real chip. The gate uses the std dev within each window instead. Slow steady rotation is still caught, because the accelerometer direction drifts and the accel gate rejects the window.
- **Sign convention:** pitch = atan2(−ax, √(ay² + az²)), roll = atan2(ay, az).
- **Baseline time budget:** 25 s for the baseline plus 10 s for the reading stays inside the 45 s watchdog.

## 2026-10-14: #2 Test harness and wake-cycle skeleton (commit b7e6326)

- **PC tests:** a `native` PlatformIO env runs Unity tests on the PC. Suites are split into `test/native/` (PC) and `test/embedded/` (board), and each env only runs its own folder.
- **Wake-cycle core:** `lib/WakeCycle/` holds `runWakeCycle()`, which has no hardware access. It talks to the board through six ports: Imu, Clock, Store, Uplink, CamTrigger and Sleeper. Each port has a fake in `test/native/fakes/Fakes.h`.
- **Board side:** `lib/Esp32Ports/` holds the real ports, most of them still placeholders. `setup()` runs one cycle and passes the result to the sleeper. Deep sleep itself is still a log line until #4.
- **Config:** all of the spec's tunables are in `include/config.h`, which no longer includes Arduino headers so the PC tests can use it.
- **Secrets:** `include/secrets.h` is git-ignored. Copy it from `include/secrets.example.h`.

**Improvements made along the way:**

- **Toolchain fix.** The `gcc.exe` in `~/bin`, first on PATH, is a bare gcc 8.1 without its C headers, so it can't compile anything. `scripts/native_toolchain.py` puts the complete TDM-GCC 10.3 at `~/tdm-gcc/bin` first, for the `native` env only. Set the `NATIVE_GCC_BIN` environment variable to use a different compiler.
- `platformio.ini`: a shared `[env]` adds `-Iinclude`, so `lib/` modules can see `config.h`. Your `upload_speed = 921600` was kept.
- CLAUDE.md fixes:
  - removed a wrong `esp32dev-release` command
  - added the test commands, including how to run one suite
  - described the wake-cycle architecture
- Project skills updated after the code review:
  - `pio-test`: covers native and embedded tests
  - `pio-add-lib`: hardware libraries go in `[env:esp32dev]`, not the shared `[env]`
  - `esp32-module`: decision logic belongs in the wake-cycle core

## Review follow-ups (not done yet)

From the #2 code review. These are judgement calls, to pick up when a ticket touches the code:

- Fakes can't yet simulate every failure. For example, the Store's saves always succeed, and the Uplink has one up/down switch for both readings and alerts. Add these when a ticket's tests need them (#7, #8).
- A `SleepPlan` type would bundle "timer + motion wake", which currently travel as two loose values.
- A small pitch/roll type would remove the repeated pitch/roll pairs in `Baseline`, `Reading` and `TempCalibration`.
- `WakeResult::level` duplicates `reading.level`.
- `Esp32Ports` (a `ports` member) and `FakeHardware` (a `ports()` method) wire the six ports differently. Pick one shape.
