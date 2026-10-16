# Improvements and changes

A running log of what changed, the decisions made along the way, and what's still open. Newest first. The spec is GitHub issue #1, and each ticket is its own issue.

## Open questions

These need a decision from the installer.

- **Timezone.** `config::TIMEZONE` is `"UTC0"`, so the photo slots (07:00, 12:00 …) would fire at UTC times. Set it to the site's POSIX TZ string.
- **Values the spec doesn't set** (placeholders in `include/config.h`, marked "Not set by spec"):

  | Constant | Value |
  |---|---|
  | `SHOCK_PHOTO_MIN_GAP_S` | 30 min |
  | `CAM_PULSE_MS` | 50 ms |
  | `WINDOW_SAMPLES` | 50 |
  | `BASELINE_WINDOWS` | 20 |
  | `BASELINE_TIME_LIMIT_MS` | 25 s |
  | `IMU_SAMPLE_RATE_HZ` | 100 Hz |

## Still to verify on the board

- [ ] `pio test -e esp32dev`, with the MPU6050 wired and the board resting flat. This runs `embedded/test_imu` and `embedded/test_store_reboot`. Both compile but haven't run yet.
- [ ] #3 manual check: flash, type `read`, tilt the board by hand, then put it back. The delta should move in the expected direction and return to about 0°. Also check which way is "positive" pitch and roll on the real mounting.
- [ ] #16 hello test: flash, then `.venv/Scripts/python.exe -m pytest test/hil/board`. `@boot` should arrive within 5 s with the right `fw`, and `ping` should get its `@pong`. Also confirm the DTR/RTS reset works on this DevKit (no need to press EN).

- [ ] #17 reading tests: board resting still on a firm surface, then `.venv/Scripts/python.exe -m pytest test/hil/board -s`. Note the printed pitch/roll spread over 5 reads (the real repeatability), and the `g=` and `noise=` values. If the tolerances turn out too tight or too loose, change the named constants at the top of `test/hil/board/test_reading.py`. Run `test_imu` and `test_store_reboot` (`pio test -e esp32dev`) in the same session.
- [ ] #17 manual hand tilt: `.venv/Scripts/python.exe -m pytest test/hil/board -k hand_tilt --manual -s`. Confirms on the real mounting that raising the +X edge gives negative pitch and raising the +Y edge gives positive roll (what the maths says). If the MPU6050 is mounted rotated, the instructions or the convention need changing.

- [ ] #18 sleep tests: `pio run -e esp32dev_hil -t upload`, then `.venv/Scripts/python.exe -m pytest test/hil/board`. The whole board suite (hello, reading, sleep) should pass on this build. `test_sleep.py` should see `@sleep s=30` about 10 s after the window opens, then `@boot cause=Timer` about 30 s later with the same `boot=` and a higher `seq=` (boot count changed in #19), then `@sleep` again with no `@pong` in between. If the wake comes noticeably early or late, the RC-oscillator tolerance (`TIMER_EARLY_S`/`TIMER_LATE_S` in the test) may need widening. Flash `esp32dev` again afterwards.
- [ ] #18 normal build by hand: flash `esp32dev`, press EN, and watch the monitor. With nothing typed, `@sleep s=900` should appear about 60 s after the reading line. Type `status` within the 60 s and it should stay awake for 5 min after that. Also check the board wakes after 15 min with `cause=Timer`, and that `@sleep` comes out complete (not cut off by the sleep).

- [ ] #19 Supabase: follow `supabase/SETUP.md` (project, `schema.sql`, URL and key in `include/secrets.h`).
- [ ] #19 upload test: `.venv/Scripts/python.exe -m pytest test/hil/board/test_upload.py`. One row with the run's tag, `time_synced=true`, values matching `@read`, and the device key refused for update and delete. Also check that the HTTP status Supabase gives for a refused update or delete is an error (the schema revokes the privileges, so it should be 401/403, not 200 with no rows).
- [ ] #19 timing of the existing board tests: every wake now uploads. With WiFi in range a wake takes a few seconds more (join + NTP + TLS). Check that hello (45 s), reading (45 s first reply) and sleep (first cycle 35 s, and `TIMER_WAKE_SLEEPS_WITHIN_S`, now 100 s, after a timer wake) still pass on the board. **With no network in range the first-reply limits will fail**, because the power-on wake then spends 2 × 30 s trying WiFi before the window opens. Run the board suite with a network from `secrets.h` in range.
- [ ] #19 certificate check: the upload test passing proves the board accepts Supabase's chain against `RootCa.h` (`setCACert`). Also watch the log once for `Supabase insert failed: HTTP -1` (a TLS failure would show there).
- [ ] #19 eduroam: at school, check the board joins `eduroam` with the PEAP entry (log `WiFi: joined eduroam`), and that a `wifi` command typed afterwards still joins the home network at home (the port turns Enterprise mode off before a Personal join).
- [ ] #19 non-blocking `wifi`: type `wifi`, then `ping 1` straight away. `@pong 1` should come after `@upload` (input waits while the command runs), and the board must not sleep in the middle of a long `wifi`.

## 2026-10-16: #19 Upload every reading to Supabase

- **Boot count (`0758553`):** counts cold boots only and is kept in flash (NVS, through `Store`). `seq` stays in RTC memory and restarts each cold boot. `@boot` gains `seq=`. User-approved test changes: see the #18 entry.
- **Core:** every wake takes its reading, then connects (`Uplink::connect()`), syncs the time (`Clock::sync()`), uploads the one reading and turns the radio off (`disconnect()`, always called). A failure (no network, or the server refuses) sets `uploaded=false` and the reading is dropped: no buffer yet. `Reading` gains `testTag`. The `wifi` command (`Command::Wifi`) connects first, then reads and uploads with the tag. No other command uses WiFi. PC suite: `native/test_upload` (11 tests).
- **Reading encoder (`lib/WakeCycle/src/ReadingJson.*`):** one reading as the JSON row of the readings table: every #1 field, `ts` as ISO 8601 UTC, NaN as null, `batt_v` always null, `test_tag` as text or null. It returns 0 rather than a cut-off row when the buffer is too small. PC suite: `native/test_reading_json`.
- **Tagged lines:** `@upload ok=<0|1> tag=<n|none> seq=<n> boot=<n>` after every upload attempt (wake cycle and `wifi`). `wifi` prints `@read` first.
- **ESP32 uplink (`Esp32Uplink`, `Esp32Clock` in `lib/Esp32Ports`):** tries the first `WIFI_MAX_NETWORKS` (2) of `secrets::WIFI_NETWORKS` in order, `WIFI_CONNECT_TIMEOUT_S` (30 s) each. NTP (`pool.ntp.org`, `time.google.com`, up to 5 s). HTTPS POST to `<SUPABASE_URL>/rest/v1/readings` with `apikey` (and `Authorization: Bearer` for a legacy `eyJ...` anon key), 10 s timeout. `isSynced()` is true once the clock is past 2023-11-14, so time set by an earlier NTP sync and carried through deep sleep counts as synced.
- **Supabase:** `supabase/schema.sql` (readings table, unique (`device_id`, `seq`, `boot_count`), RLS: anyone reads, the anon key only inserts, update/delete/truncate privileges revoked, a `real_readings` view without test rows) and `supabase/SETUP.md` (a first-time checklist, with troubleshooting).
- **Secrets:** `secrets.example.h` has two networks (home, school; the phone hotspot is out of scope; see the rework below for their shape), the Supabase URL and key, the ntfy topic and `DEVICE_ID`. The firmware fails to build with a clear `#error` if `include/secrets.h` is missing. CI copies the template.
- **Board tests (`test/hil/board/test_upload.py`):** `wifi <random tag>` must give `@read ok=1` and `@upload ok=1`, then exactly one row with that tag, `time_synced`, `ts` after the test start (5 s clock skew allowed) and not in the future, the printed pitch/roll/tilt/noise/temp/stable, the right `device_id`, `fw`, `seq` and `boot_count`. A second test, which needs no board, inserts a tagged row with the device key and checks that update and delete are refused and the row is unchanged. The PC reads `SUPABASE_URL`, `SUPABASE_KEY` and `DEVICE_ID` from `include/secrets.h` (`test/hil/supabase_rest.py`, standard library only). Example values still in place make the test FAIL, never skip.
- **Self-check (`selfcheck/test_upload_selfcheck.py`, `fake_supabase.py`):** both tests pass against a healthy fake board and database, and FAIL for: no row, two rows, a row under another tag, a row older than the test or in the future, unsynced time, each value differing from `@read`, another `fw`, `@upload ok=0`, a silent port, a board without `wifi`, and a key that can update (openly or silently), delete or not insert. The secrets parsing is checked against the real template.
- **Build:** `esp32dev` (and so `esp32dev_hil`) now builds with `-DCORE_DEBUG_LEVEL=2`. The Arduino default is no logging at all, so `log_e`/`log_w` (for example why an upload failed) never printed.
- **Decisions to review:**
  - **Dating back:** a reading taken before the first sync (the first wake after power-on) gets real time once the sync succeeds in the same wake: now minus the uptime since it was taken, and `time_synced=true`. Only a reading whose wake doesn't sync stays `time_synced=false` with time since power-on.
  - NTP is synced on every connected wake (spec #1), not just the first.
  - `wifi <n>` uses the given tag (the board test passes its own), plain `wifi` picks a random 9-digit one.
- **Not done:** no on-board run yet (no Supabase project exists). The board timing concern for the existing tests is in "Still to verify on the board".
- **Rework (Handoff 2 on #14, and the user's answers):**
  - **WiFi from the user's code.** The home (WPA2-Personal) and school (eduroam, WPA2-Enterprise PEAP) connect code the user wrote is now inside `Esp32Uplink`, with the credentials in `include/secrets.h`. `secrets::WifiNetwork` is `{auth, ssid, password, identity, username}` with `WifiAuth::Personal` or `WifiAuth::EnterprisePeap`; entries are written `personal("ssid", "password")` or `peap("ssid", "identity", "username", "password")`. PEAP joins with `WiFi.begin(ssid, WPA2_AUTH_PEAP, identity, username, password)`. The core leaves Enterprise mode on after a PEAP attempt, so a Personal join calls `esp_wifi_sta_wpa2_ent_disable()` first.
  - **Certificate check.** `setInsecure()` is gone. `sendReadings` calls `setCACert(root_ca::HTTPS_ROOTS)` from `lib/Esp32Ports/src/RootCa.h`, one concatenated PEM that #20 (ntfy) reuses, and sets the TLS handshake timeout to `HTTP_TIMEOUT_MS` (the core's default is 120 s). Checked on 2026-10-16 with `openssl s_client -showcerts`:
    - Supabase project hosts (`<ref>.supabase.co`, three real projects): `WE1` ← **GTS Root R4** (Google Trust Services). crt.sh also shows `supabase.co` certificates from `WR1` ← **GTS Root R1**, and `supabase.co` itself served from Let's Encrypt.
    - `ntfy.sh`: `YR1` ← Root YR ← **ISRG Root X1** (Let's Encrypt).
    - The bundle: GTS Root R1 (expires 2036-06-22), GTS Root R4 (2036-06-22), ISRG Root X1 (2035-06-04), ISRG Root X2 (2040-09-17), copied from the Mozilla bundle (certifi 2026.07.22). Supabase is behind Cloudflare, which issues from either Google Trust Services or Let's Encrypt, so both are included. With the bundle (and only it) as the trust store, openssl verifies the project hosts, `supabase.co` and `ntfy.sh`; GTS Root R4 alone verifies a project host, ISRG Root X1 alone verifies ntfy.sh, and neither verifies the other.
    - **If Supabase or ntfy.sh move to another certificate authority, every upload fails with a TLS error until `RootCa.h` is updated** (rerun the openssl check above and add the new root).
  - **Non-blocking waits (user decision).** `Uplink` and `Clock` gained two-step versions of their slow calls: `startConnect()`/`pollConnect()` and `startSync()`/`pollSync()`, which return at once and report `Progress::Pending`, `Done` or `Failed`. The ESP32 ports time them with `millis()` (`WIFI_CONNECT_TIMEOUT_S` per network, `NTP_SYNC_TIMEOUT_MS`); there is no `delay()` left in them. The `wifi` command is a small state machine in the core, `WifiCommand` (joining, then syncing, then read + upload + radio off): `main.cpp` starts it and `loop()` polls it once per pass, printing `@read`/`@upload` when it finishes. `CommandWindow::commandStarted()` keeps the board awake while it runs, and typed input waits in the UART buffer until it is done. `runCommand(Command::Wifi)` drives the same state machine to the end. The wake cycle in `setup()` keeps the waiting `connect()`/`sync()`, which now just poll the two-step calls (allowed by the user's answer). The reading itself and the HTTPS post are still single calls of a few seconds each (bounded by `READING_TIME_LIMIT_MS` and `HTTP_TIMEOUT_MS`), as for `read`. PC suite: `native/test_wifi_command` (5 tests).
  - **Board sleep test (user-approved change).** `TIMER_WAKE_SLEEPS_WITHIN_S` in `test/hil/board/test_sleep.py` was 15 s. It is now derived from `include/config.h` through `harness.config_number()`: reading (10 s) + `WIFI_MAX_NETWORKS` × `WIFI_CONNECT_TIMEOUT_S` (60 s) + NTP (5 s) + 2 × `HTTP_TIMEOUT_MS` (20 s, TLS connect/handshake and response) + 5 s spare = **100 s** today. Three self-check tests were added for this (110 in all). No other existing test was changed.
  - **Open:** the eduroam join passes no RADIUS server CA (the user's code didn't either), so the board doesn't check it is talking to the real school server before sending the account password. `WiFi.begin` takes a `ca_pem` argument for this; it needs the school's RADIUS CA.

## 2026-10-16: #18 Deep sleep with the command window

- **Core (`CommandWindow` in `lib/WakeCycle`):** decides when an awake board may sleep. Power-on/reset wakes listen for `CycleConfig::commandWindowMs` (60 s). Each handled command keeps the board awake until `sessionIdleMs` (5 min) after it, and never shortens a longer window. Timer and motion wakes don't listen, so sleep is due at once. It works on `millis()` values and survives the wrap-around. PC suite: `native/test_command_window` (8 tests, including `@sleep s=` formatting).
- **Config:** new `COMMAND_WINDOW_MS` = 60 s, and `SERIAL_SESSION_IDLE_MS` goes from 60 s to 5 min.
- **Firmware:** `setup()` runs the wake cycle and then opens the window. `loop()` checks the window first, then reads commands without blocking, and reports each one to the window *after* handling it. When the window closes it prints `@sleep s=<seconds>`, flushes the UART and calls the Sleeper. `Esp32Sleeper` does real deep sleep with a timer wake (a 0 s timer becomes 1 s so the board always wakes). The wake cause was already read from the chip (`esp_sleep_get_wakeup_cause`). `RtcState` is `RTC_DATA_ATTR`, so it survives deep sleep and is reset on power-on/EN.
- **Test build:** `[env:esp32dev_hil]` extends `esp32dev` and adds `-DESPSPRINT_HIL_TIMINGS`. `config.h` then uses a 30 s interval, a 10 s window and a 20 s session idle. Nothing else differs. CI builds both envs.
- **Board tests (`test/hil/board/test_sleep.py`, needs the esp32dev_hil firmware):**
  - with no command: `@sleep s=30` no sooner than the window after boot, then `@boot cause=Timer` about 30 s later (−5/+15 s for the RC oscillator), with the boot count carried on (RTC kept). A `ping` sent straight after the timer wake gets no `@pong` before the next `@sleep`.
  - with a command: no `@sleep` for 15 s (past the 10 s window), still answering `ping`, then `@sleep` 20 s after the last command.
  - Against the normal firmware they fail with "no @sleep ... is the esp32dev_hil firmware flashed?".
- **Harness (additions only):** `Board.now()` and `Board.watch(seconds)`.
- **Self-check (`selfcheck/test_sleep_selfcheck.py`, `fake_sleepy_firmware.py`):** a fake HIL board on a shared fake clock. Both tests pass on a healthy board (also with a 35 s first cycle), and FAIL against a silent port, a board that never sleeps, the normal esp32dev timings, a wake cause other than Timer (PowerOn = a reset instead of sleep), a wake 3 s or 90 s after `@sleep`, the wrong `s=`, no command window, a lost boot count, a timer wake that answers commands, commands that don't extend the window, and an idle time of 16 s or 60 s.
- **Existing board tests:** hello and reading send a command as soon as the board answers, which keeps it awake (20 s idle on esp32dev_hil, 5 min on esp32dev). Commands typed during the first wake cycle wait in the UART buffer and are handled when the window opens. **Timing concern:** the manual hand-tilt test waits for the installer between commands. On esp32dev_hil the board sleeps if a step takes over 20 s, so run the manual test on the normal esp32dev firmware.
- **Decisions:**
  - The window opens when the boot's wake cycle ends, not at reset, so the installer gets the full 60 s even after a 35 s first-boot baseline.
  - The idle time is counted from the end of a command, so a slow `baseline` (up to 25 s) doesn't use up the idle time.
  - The esp32dev_hil session idle is 20 s, not 5 min, so the "sleeps after the last command" check takes seconds.
  - The timer is the full interval after the window closes. Time spent awake isn't subtracted.
  - Motion wakes are treated like timer wakes (no window). Wake-on-motion itself is still out of scope (#5).
- **Not done:**
  - The 45 s wake watchdog from #4 is skipped. It's optional for this ticket, and a single watchdog for the whole wake would conflict with the 5 min command window.
  - **Boot count on cold boot only (open question):** the ticket says the boot count should go up only on a cold boot, but the existing PC test `test_boot_count_carries_across_timer_wakes` expects it to go up on every wake. Existing tests aren't changed without asking, so the count still goes up on every boot, and the board sleep test checks that (`boot=2` after the timer wake). With RTC memory reset on power-on, "cold boots only" would need the count in flash (NVS). It's needed for the (`device_id`, `seq`, `boot_count`) uniqueness from #1. **Resolved in #19:** the user approved counting cold boots only, kept in flash.

## 2026-10-16: #17 Tilt reading checked on the board over serial

- **Tagged replies:** `read` prints `@read ok= pitch= roll= tilt= stable= noise= temp= g=`. `baseline` and `status` print `@status ok= baseline= [bpitch= broll=] pitch= roll= tilt= stable= noise= temp= g=`, where bpitch/broll appear only when a baseline is stored. The human-readable lines are still printed. A failed `baseline` now prints `@status ok=0` as well as the old "failed" line. The formatting lives in the core (`lib/WakeCycle/src/TaggedReply.*`), so the PC tests check the exact line.
- **Core:** `Reading` gains `accelG`, the magnitude of the averaged acceleration (≈ 1 g at rest), used by the "total ≈ 1 g" board test. Readings now fill `tiltDeg` = √(pitch² + roll²) of the delta (the "combined" tilt of spec #1), and `tempC` from the IMU (NaN if the IMU doesn't answer). Both were previously left at 0. New PC suite: `native/test_read_reply`.
- **Board tests (`test/hil/board/test_reading.py`):** a still board reads 1 ± 0.1 g. It gives `stable=1` with 0 < noise ≤ 0.003 g. The delta is within ±0.25° straight after `baseline`. 5 reads in a row agree within a 0.1° spread. Plus a manual hand-tilt test (`--manual`). Tolerances are named constants with the reasoning next to them.
- **Stale-reply guard:** before each command, the test sends `ping <random>` and waits for its `@pong`. Commands are handled one at a time, so a `@read`/`@status` read after that pong must be a reply to this command, never earlier output.
- **Harness (additions only):** `Board.sync()`, `Board.ask()`, `Tagged.number()`, `random_id()`, and `Installer` (story 14: each manual step prints clear instructions, then waits for Enter, and FAILS if stdin is closed). The `installer` fixture in `conftest.py` suspends pytest's output capture around each step, and refuses to run in a test not marked `manual`, so the automatic run never waits for a person.
- **Self-check:** `selfcheck/test_reading_selfcheck.py` runs every reading test against a fake still-board firmware (`selfcheck/fake_tilt_firmware.py`). Each must pass on a healthy board, and FAIL against: a silent port, a board that ignores the command (with a stale `@read` queued), a board whose only `@read` was printed before the command, a wrong total (0.5/0.85/1.15/2 g), `stable=0`, noise of 0 or above the gate, a baseline that isn't subtracted, or fails to capture, and scattered reads. The hand-tilt test is checked for the wrong pitch or roll sign, no movement, and no return to 0°. `selfcheck/test_manual_helper.py` checks the helper end to end in a pytest subprocess: instructions show and Enter is waited for despite output capture, and the step fails with stdin closed.
- **Limit:** if the board happens to sit exactly level, the "delta ≈ 0 after baseline" test can't tell a missing baseline subtraction apart. The PC tests cover the subtraction.

## 2026-10-16: #16 Board test platform: tagged replies, boot banner, ping, PC harness

- **Firmware:** `startBoot()` in the wake-cycle core counts boots in RTC state. `setup()` prints `@boot fw=<ver> boot=<n> cause=<PowerOn|Timer|Motion|Serial>` straight after `Serial.begin()`, before the IMU and the wake cycle. `ping <n>` replies `@pong <n>`. The serial reader now keeps spaces inside a line, so commands can take an argument. It still trims leading and trailing spaces.
- **PC test:** `native/test_boot` checks boot counting and the cause names.
- **Python environment:** run `~/.platformio/penv/Scripts/python.exe scripts/setup_venv.py` once. It builds a git-ignored `.venv` from PlatformIO's Python and installs `test/hil/requirements.txt` (pytest, pyserial).
- **Harness (`test/hil/harness.py`):**
  - finds the port by USB chip (CP210x, CH340, CH9102, FT232R), or uses `ESP_PORT` if it's set
  - FAILS with the list of ports it checked when there's no board, or asks for `ESP_PORT` when there are two
  - resets the board through RTS/DTR, and clears serial input while EN is held low
  - parses `@tag key=value` lines and ignores all other lines
  - `manual` tests are skipped unless `--manual` is given
- **Suites:** `test/hil/board` needs the board. `test/hil/selfcheck` runs with no board. Among other things, the self-check runs the real hello test against:
  - a silent fake port, which must FAIL
  - stale `@boot` text from before the reset, which must FAIL
  - a wrong firmware version, which must FAIL
  - a fake firmware that answers correctly, which must pass (the positive control)
- CI (#15's workflow) has a new step, "Board-test harness self-check". It runs `scripts/setup_venv.py` and then `pytest test/hil/selfcheck` on Linux.
- `platformio.ini` gains `test_ignore = hil/*`, so `pio test` never treats the pytest folder as a suite.
- **Decision:** `ping` stays in `main.cpp` and doesn't go through `runCommand()`. It's a pure echo with no decision logic, and adding an argument to `Command` would change the `runCommand()` signature that the existing tests use.
- **Note:** the first wake cycle runs before `loop()` reads commands. On a board with no stored baseline, that can take up to 35 s, so the hello test waits up to 45 s for `@pong`. #17's command window will change this.

## 2026-10-16: #15 CI on GitHub Actions

- `.github/workflows/ci.yml` runs on every push and pull request, on `ubuntu-latest` with Python 3.11. It installs PlatformIO with pip, then runs `pio test -e native` and `pio run -e esp32dev`. Either step failing turns the run red.
- `~/.platformio` (cached by a hash of `platformio.ini`) and pip downloads are cached.
- CI copies `include/secrets.example.h` to `include/secrets.h`, so the build keeps working once code includes it. Nothing includes it yet.
- `scripts/native_toolchain.py` changes PATH only if `~/tdm-gcc/bin` exists, so on Linux the system gcc is used. The native code and tests use only standard C++17 headers, and the include paths match the file names' case.
- Checked locally on Windows: 13/13 native tests pass and `esp32dev` builds. The workflow itself hasn't run yet; check the first run on GitHub.

## 2026-10-16: Scope narrowed and board test platform planned (spec #14)

Decided in a grilling session with the installer. Nothing is coded yet. The code changes listed here are still to do.

**Core build:** read, sleep, upload, alert. Order:

1. Platform work that doesn't need the board.
2. Reading and deep sleep on the board.
3. WiFi upload of every reading.
4. Bare levels and ntfy alerts.

**Test platform** ([ADR 0001](adr/0001-board-tests-from-the-pc-with-false-positive-guards.md)):
- The main seam is now **the whole firmware over USB serial**, tested by pytest + pyserial on the PC.
- The wake cycle on the PC (called "seam 1" in the #3 entry below) is now seam 2, and the on-board driver tests are seam 3.
- Replies use `@tag key=value` lines.
- No board means FAIL. Every reply must echo this run's random number. Upload and alert tests match this run's tagged record.
- CI runs the PC tests and the firmware build.
- Each feature gets one branch and pull request, and merges only when CI and the board suites pass.

**Decisions:**

- **Accuracy** ([ADR 0002](adr/0002-accept-0.5-degree-accuracy-before-calibration.md)): aim for 0.25° and accept 0.5° for now. No temperature compensation yet: the correction stays at zero, and raw values are still recorded. `WATCH_DEG` goes from 0.5° to **1.0°** until calibration.
- **WiFi:** 2 networks (home, then school), **30 s each**. This closes the open question about 2 or 3 networks; the phone hotspot is dropped for now. `WIFI_MAX_NETWORKS` stays at 2, and `WIFI_CONNECT_TIMEOUT_S` goes from 20 to 30. The installer may swap in their own WiFi code later, behind `Uplink`.
- **Command window:** after power-on or reset, the board listens for 60 s, then sleeps. Each command keeps it awake until 5 min after the last command (`SERIAL_SESSION_IDLE_MS` goes from 60 s to 5 min). Timer wakes don't listen.
- **Test build:** an `esp32dev_hil` environment with short timings, so real deep sleep can be tested quickly.
- **Uploads:** every reading is uploaded straight away. A failure is logged as `@upload ok=0` and the reading is lost. The offline buffer and hourly batching come later, as a simple buffer stage.
- **Supabase:** no project exists yet. A schema file in the repo, with a nullable `test_tag` column and insert-only access for the device key, plus a setup checklist for a first-time user.
- **Alerts:** a bare version of #8 (levels, margin, ntfy only when the level goes up, retry). Test alerts go to the real topic, with a "TEST — not a real alert" line. Event mode and the CAM pulse are deferred.
- **Deferred:**
  - temperature compensation (#11)
  - the offline buffer and fault handling (#7)
  - motion wake (#5)
  - the dashboard (#9)
  - the CAM (#10)
  - the jig (#12), which becomes a few fixed 3D-printed angles after the main build

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
