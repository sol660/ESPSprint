# Improvements and changes

A running log of what changed, the decisions made along the way, and what's still open. Newest first. The spec is GitHub issue #1, and each ticket is its own issue.

## Open questions

- ~~**Accelerometer reads 0.876 g at rest**~~ **Addressed 2026-10-16 (user: calibrate now):** six-position calibration, #22 (see below). `embedded/test_imu` still reads the raw sensor and keeps failing on this module (user decision).
- **No Supabase column for the calibration.** A reading corrected by `cal` and one before it look the same in the `readings` table (`@read` shows `cal=`). Adding an `accel_cal` column needs a `schema.sql` change in the live project.
- **Large tilts read about 1.6× high (to check).** In the alert-queue check (2026-10-16, before calibration, against a baseline also taken before calibration) the installer lifted the board on a 93 mm pivot length with 19 mm and 46 mm objects: true asin(h / 93) = 11.8° and 29.6°, reported tilt delta 19.7° and 48.1°. A rough model of the −0.152 g Z offset only explains about 13–14° and 33°. Next step: repeat with the calibrated board, keeping one serial session open after a flat reset (no cold boot while tilted, so no ALERT), then `read` on each object, then put the board back flat before its next timer wake. Near 11.8°/29.6° means it was the offset plus the old baseline; still ~1.6× means look at the angle maths or the mounting before trusting the thresholds. The wedge jig (#12) covers small angles.
- **Cross-axis error isn't calibrated.** Six positions give offset and scale per axis only. A module whose axes aren't quite square would need a 3 × 3 fit with more positions.

From #7 (offline buffer and sensor failure):

- **`CycleConfig::bufferCapacity` defaults to 0 (no buffer); the device turns it on with `deviceCycleConfig()`.** Three existing tests pin the first build's "a failed upload drops the reading" (`native/test_upload`: `test_no_network_drops_the_reading_without_buffering`, `test_server_refusing_the_reading_drops_it`; both assert `hw.store.buffer.size() == 0`). They weren't changed (no approval), so the old behaviour stays visible to them through the default. Cleaner would be to default to `config::BUFFER_CAPACITY` and update those two tests: **needs the user's approval.**
- **Hourly batching is not done.** The #14 scope note says "Hourly batching returns with it", but #7's acceptance criteria don't list it, and the existing tests pin "every wake uploads" (`test_timer_wakes_upload_too`, the board timing tests). Left out; it needs its own ticket or a decision.
- **A row Supabase always refuses blocks the buffer.** If one buffered reading is refused for its content (HTTP 400, for example a schema mismatch in the live project), every post containing it fails, so the buffer stops draining and new readings queue behind it until it is full, then the oldest are dropped. I chose not to drop refused rows: a schema mismatch would refuse every row, and dropping would lose them all, while a stuck buffer is visible (`@upload ok=0`, `log_e` with Supabase's reply, the dashboard's OFFLINE badge) and recovers once the schema is fixed. Decide whether a "skip a row refused on its own" rule is wanted.
- **Buffered readings from before the first time sync keep time since power-on.** Only a reading synced in its own wake is dated back. One taken offline after a power loss (no sync yet) goes up later with `time_synced=false` and a 1970-based `ts`. It could be dated back from the clock jump at the first sync if it has the same `boot_count`; not done.
- **No `event=` in `@read`**, because `native/test_read_reply` checks the exact `@read`/`@status` strings, so adding a key would change those tests. A sensor error shows in the human line ("SENSOR ERROR ...") and in the Supabase row (`event=sensor_error`). For the same reason the buffer count is a separate `@buffer` line after `@status`, not a key in it.

From the spec #14 code review (left as they are, for a decision):

- ~~**`time_synced` on a back-dated reading.**~~ **Resolved 2026-10-16 (user: follow the spec):** a reading taken before the first sync keeps its dated-back `ts` but stays `time_synced=false`. The two tests that asserted the old behaviour were changed with the user's approval (#14).
- **The HTTPS post still blocks `loop()`** inside the `wifi` and `alert` commands, for up to 2 × `HTTP_TIMEOUT_MS` (TLS handshake, then the response). Only the WiFi join and NTP waits are polled.
- **`setup()`'s power-on wake waits on WiFi** (up to 2 × `WIFI_CONNECT_TIMEOUT_S` + NTP + HTTPS) before the command window opens.
- **HIL `SERIAL_SESSION_IDLE_MS` is 20 s** in `esp32dev_hil`, short for typing by hand on that build.
- ~~**`CORE_DEBUG_LEVEL=2`**~~ **Resolved 2026-10-16: kept** (user). Warnings and errors stay on in the shipped build for field debugging.
- **Alert merge: accepted 2026-10-16** (user). A WATCH still waiting is folded into a later ALERT, so only the ALERT is sent; same or lower levels are never folded.
- **PEAP without a RADIUS server CA**: the eduroam join doesn't check the RADIUS server's certificate.
- ~~**`test_upload.py`'s `FIRST_REPLY_WITHIN_S` is hard-coded**~~ **Resolved 2026-10-16:** derived from `config.h` (user approval). Its `WIFI_WITHIN_S` is still written out by hand.

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

**Before any board-test session:** put the board flat where it will stay and type `baseline`. The board tests reset it many times, and every reset restarts the level at OK, so with a stale baseline each reset raises a real ALERT (sent to the phones, or queued while offline).

- [x] `pio test -e esp32dev`, with the MPU6050 wired and the board resting flat. This runs `embedded/test_imu` and `embedded/test_store_reboot`. Both compile but haven't run yet. **2026-10-16 (core-build `d7f40aa`, HIL build):** `test_store_reboot` passed; `test_imu`: I2C and temperature passed, **1 g failed: 0.876 g at rest** (see the open question below).
- [ ] #3 manual check: flash, type `read`, tilt the board by hand, then put it back. The delta should move in the expected direction and return to about 0°. Also check which way is "positive" pitch and roll on the real mounting.
- [x] #16 hello test: flash, then `.venv/Scripts/python.exe -m pytest test/hil/board`. `@boot` should arrive within 5 s with the right `fw`, and `ping` should get its `@pong`. Also confirm the DTR/RTS reset works on this DevKit (no need to press EN). **2026-10-16 (core-build `d7f40aa`, HIL build):** passed; the DTR/RTS reset works with no EN press.

- [x] #17 reading tests: board resting still on a firm surface, then `.venv/Scripts/python.exe -m pytest test/hil/board -s`. Note the printed pitch/roll spread over 5 reads (the real repeatability), and the `g=` and `noise=` values. If the tolerances turn out too tight or too loose, change the named constants at the top of `test/hil/board/test_reading.py`. Run `test_imu` and `test_store_reboot` (`pio test -e esp32dev`) in the same session. **2026-10-16 (core-build `d7f40aa`, HIL build):** stable, delta ≈ 0 after `baseline` and repeated reads passed. **`test_still_board_reads_about_1g` failed: `g=0.8758`** (noise 0.0017 g, temp 21.5 °C). The scaling is the datasheet's ±2 g = 16384 LSB/g, so this looks like a sensor offset or gain error on this module, not firmware. Raw roll was +11.2°, so the board wasn't flat.
- [ ] #17 manual hand tilt: `.venv/Scripts/python.exe -m pytest test/hil/board -k hand_tilt --manual -s`. Confirms on the real mounting that raising the +X edge gives negative pitch and raising the +Y edge gives positive roll (what the maths says). If the MPU6050 is mounted rotated, the instructions or the convention need changing.

- [x] #18 sleep tests: `pio run -e esp32dev_hil -t upload`, then `.venv/Scripts/python.exe -m pytest test/hil/board`. The whole board suite (hello, reading, sleep) should pass on this build. `test_sleep.py` should see `@sleep s=30` about 10 s after the window opens, then `@boot cause=Timer` about 30 s later with the same `boot=` and a higher `seq=` (boot count changed in #19), then `@sleep` again with no `@pong` in between. If the wake comes noticeably early or late, the RC-oscillator tolerance (`TIMER_EARLY_S`/`TIMER_LATE_S` in the test) may need widening. Flash `esp32dev` again afterwards. **2026-10-16 (core-build `d7f40aa`, HIL build):** both sleep tests passed (timer wake seen); `esp32dev` flashed back afterwards.
- [ ] #18 normal build by hand: flash `esp32dev`, press EN, and watch the monitor. With nothing typed, `@sleep s=900` should appear about 60 s after the reading line. Type `status` within the 60 s and it should stay awake for 5 min after that. Also check the board wakes after 15 min with `cause=Timer`, and that `@sleep` comes out complete (not cut off by the sleep).

- [x] #19 Supabase: follow `supabase/SETUP.md` (project, `schema.sql`, URL and key in `include/secrets.h`). **2026-10-16 (`e45e028`):** project created, schema run, publishable key in `secrets.h`.
- [x] #19 upload test: `.venv/Scripts/python.exe -m pytest test/hil/board/test_upload.py`. One row with the run's tag, `time_synced=true`, values matching `@read`, and the device key refused for update and delete. Also check that the HTTP status Supabase gives for a refused update or delete is an error (the schema revokes the privileges, so it should be 401/403, not 200 with no rows). **2026-10-16 (`e45e028`):** both tests passed (one row with the run's tag; update and delete refused).
- [x] #19 timing of the existing board tests: every wake now uploads. Check on the board that hello, reading and sleep still pass with the limits below, both with a network from `secrets.h` in range and without one. **2026-10-16 (`e45e028`):** full board suite on `esp32dev_hil` with home WiFi in range: 9 passed, 1 skipped (manual hand tilt), only the 1 g test failed (sensor). **2026-10-16 (`662bfca`), no network in range** (a HIL build with both SSIDs in `secrets.h` swapped for names that don't exist; `secrets.h` restored straight after the flash): hello, reading and sleep 6 passed, 1 skipped (manual), only the 1 g test failed (`g=0.8687 cal=0`, before calibration). 10 min 10 s for the 7 tests.
- [x] #19 certificate check: the upload test passing proves the board accepts Supabase's chain against `RootCa.h` (`setCACert`). Also watch the log once for `Supabase insert failed: HTTP -1` (a TLS failure would show there). **2026-10-16 (`e45e028`):** the upload passed with `setCACert`, so the board accepts Supabase's chain.
- [ ] #19 eduroam: at school, check the board joins `eduroam` with the PEAP entry (log `WiFi: joined eduroam`), and that a `wifi` command typed afterwards still joins the home network at home (the port turns Enterprise mode off before a Personal join).
- [ ] #19 non-blocking `wifi`: type `wifi`, then `ping 1` straight away. `@pong 1` should come after `@upload` (input waits while the command runs), and the board must not sleep in the middle of a long `wifi`.

- [x] #20 ntfy setup: pick a hard-to-guess topic, put it in `NTFY_TOPIC` in `include/secrets.h`, and subscribe to it in the ntfy app on the installer's Android phone and the client's iPhone. **2026-10-16 (`e45e028`):** topic set in `secrets.h`, subscribed on the installer's phone (client's iPhone still to do).
- [x] #20 alert test: `.venv/Scripts/python.exe -m pytest test/hil/board/test_alert.py` (normal `esp32dev` build, a network from `secrets.h` in range). It should pass, and the phones should show one "Landslide monitor node-01: TEST" notification whose first line is "TEST — not a real alert". Check the em dash shows correctly on both phones (the body is UTF-8). This also proves the board accepts ntfy.sh's chain against `RootCa.h`; a TLS failure shows in the log as `ntfy send failed: HTTP -1`. **2026-10-16 (`e45e028`):** passed on `esp32dev` and again in the full `esp32dev_hil` run, so the board accepts ntfy.sh's chain. The installer confirmed the notification arrived on their phone with the "TEST — not a real alert" line shown correctly (client's iPhone still to check).
- [ ] #20 real alert by hand: with the board flat, type `baseline`, then tilt it by more than 2° and press EN (a power-on wake takes a reading). The boot should print `@alert ok=1 tag=none level=ALERT tilt=...` and the phones get a priority-5 "ALERT" notification with the tilt and time. Put it back flat and press EN: no notification (the level resets to OK on a cold boot, and drops never notify). **Note:** RTC memory is reset by EN, so the level and the WATCH count start again from OK after each reset; WATCH needs two timer wakes in a row (test it on `esp32dev_hil`, 30 s apart).
- [ ] #20 retry: with WiFi out of range (or a wrong password in `secrets.h`), raise an ALERT as above: `@alert ok=0`. Bring WiFi back and let the next wake run: `@alert ok=1` with the same level and tilt, delivered once, and no `@alert` line on the wake after that.
- [ ] #20 timing of the existing board tests: a wake with an alert to send makes one more HTTPS post (up to 2 × `HTTP_TIMEOUT_MS`). The limits in the hello, reading, sleep and upload tests (and `TIMER_WAKE_SLEEPS_WITHIN_S`) don't include it, so run them with the board flat after `baseline` (level OK, nothing pending). Changing those tests needs the user's approval.
- [ ] #20 non-blocking `alert`: type `alert`, then `ping 1` straight away. `@pong 1` should come after `@alert`, and the board must not sleep while the alert is being sent.
- [x] #22 calibration: `.venv/Scripts/python.exe -m pytest test/hil/board/test_calibration.py --manual -s` on `esp32dev`. **2026-10-16 (`662bfca`):** passed (9 min 40 s). Offset x +0.0359, y +0.0567, **z −0.1523 g**; scale x 0.9985, y 1.0027, z 1.0178. So the 0.87 g was mostly a Z offset (about twice the datasheet's typical worst case), not a gain error. Afterwards the full board suite on `esp32dev_hil` with WiFi: **10 passed, 2 skipped (manual), none failed**, including `test_still_board_reads_about_1g`. `pio test -e esp32dev`: `test_store_reboot` passed; `test_imu` 1 g failed at 0.868 g, as expected (raw sensor, unchanged by decision). On `esp32dev` afterwards: `read` gives `g=1.0000 cal=1`, and the flat baseline is now −0.04°/+0.12° (it was −2.97°/+5.24° uncorrected: the Z offset showed up as tilt).
- [x] Code review: alert queue in flash. On `esp32dev_hil` with WiFi out of range, raise ALERT, let it drop to OK, then raise WATCH (`@alert ok=0` each time). Bring WiFi back: the next wake should deliver both, ALERT first, and the wake after that prints no `@alert`. Also check `embedded/test_store_reboot` still passes (the Store's Preferences code was refactored). **2026-10-16 (`662bfca`), done as ALERT, flat, ALERT** (a WATCH can't be raised by hand: putting the board down is a step of 0.5° or more, so it goes straight to ALERT). Cold boots by reset on the no-WiFi build gave `@alert ok=0` each time, and the flat wake raised nothing new. After flashing `esp32dev` (WiFi back), its first boot delivered **8** alerts to ntfy, oldest first, about 2 s apart, and the next wake printed no `@alert` (`@upload ok=1`). Why 8: the queue was full (`ALERT_QUEUE_LEN`). The no-WiFi board run before it had a stale baseline about 6.4° off, so every test reset (a cold boot, level back to OK) raised and queued an ALERT. The installer's two tilts were the last two (19.7°, 48.1°). See the test-session rule above. `embedded/test_store_reboot` passed the same day.

- [ ] #7 buffer survives a reboot: `pio test -e esp32dev -f embedded/test_buffer_reboot` (it builds; not run yet). The first run formats the LittleFS partition, which can take a few seconds. Also re-run `embedded/test_store_reboot` (NVS untouched, but the same flash).
- [ ] #7 Supabase ignore-duplicates: with the live project, check a post with `?on_conflict=device_id,seq,boot_count` and `Prefer: resolution=ignore-duplicates` is accepted (201) with only the insert grant and the insert RLS policy, and that re-sending a row already there adds nothing and doesn't fail the post. `ON CONFLICT DO NOTHING` needs only the insert privilege, so no `schema.sql` change was made; if Supabase answers 401/403 here, report it before changing grants. `test_upload.py` (a single new row) should pass unchanged.
- [ ] #7 manual outage (acceptance criterion): flash `esp32dev`, turn the router off for 2 h (8 wakes: `@upload ok=0`, `@buffer count=` going 1, 2, ... 8), then on. The next wake should print `@upload ok=1` and `@buffer count=0 sent=9 dropped=0`, and Supabase should hold every `seq` of that period exactly once (`select seq, count(*) from readings where boot_count = <n> group by seq having count(*) > 1` returns nothing). Faster on `esp32dev_hil` (30 s wakes).
- [ ] #7 `clearbuf` and `status`: with readings buffered, `status` prints `@buffer count=<n> sent=0 dropped=0`; `clearbuf` prints `@clearbuf ok=1 cleared=<n> count=0`, and a following `status` shows `count=0`.
- [ ] #7 sensor failure: unplug the MPU6050's SDA (or the whole module) and press EN. The log should show "resetting the I2C bus", then "still not answering", the human line "SENSOR ERROR", and the Supabase row `event=sensor_error`, `stable=false`, with rssi/boot/reset/fw filled. Plug it back and press EN: a normal reading. Also check a wake with the sensor missing still fits the 45 s/105 s watchdog budgets (it should be quicker than a normal one).
- [ ] #7 backlog timing: a full post is 25 rows (about 10 KB of JSON) and a wake sends at most 8 posts. Time a wake that sends a large backlog (for example 200 buffered on `esp32dev_hil`) and check it stays well inside the 105 s upload watchdog; lower `UPLOAD_MAX_BATCHES_PER_WAKE` if not. Also check heap: the post body is a `String` reserved for 25 × 640 bytes.
- [ ] #9 dashboard, local: copy `dashboard/config.example.js` to `dashboard/config.js` with the real URL, public key and device id (`dashboard/README.md`), serve it (`python -m http.server 8000 --directory dashboard`) and open <http://localhost:8000/>. The status card should match the newest row of `real_readings`, and the browser console should show no errors. Only checked against made-up data so far.
- [ ] #9 publish: set up Pages as in `dashboard/README.md` (the repo is private: needs GitHub Pro / Student Pack, or a public repo), run the "Dashboard to GitHub Pages" workflow once, and check the published `config.js` holds only the public key.
- [ ] #9 loads real data in iPhone Safari and Android Chrome (both light and dark mode), and **Add to Home Screen** works on both, with the green slope icon and the page opening full screen.
- [ ] #9 OFFLINE badge on real data: leave the board unpowered for over 2 h (or look at the page after a gap) and check the badge shows; it should go away after the next upload.
- [ ] #9 events on real data: after the #20 hand alert test, the events list should show OK → ALERT, then ALERT → OK after the reset (times in local time).

## 2026-10-17: #7 Offline buffer and fault handling

Built test-first at the wake-cycle seam (PC) and the reply formatters; the Store adapter has an on-board test that hasn't run yet. **No existing test was changed**, and `Fakes.h` wasn't touched (the new suites subclass the fakes).

- **Offline buffer (core, `uploadReadings()`/`bufferReading()` in `WakeCycle.cpp`):** a wake reading that doesn't reach the server (no network, or the server refuses) is pushed to the Store's buffer. At the next connected wake the buffer is sent oldest first, then the new reading, in posts of up to `UPLOAD_BATCH_SIZE` (25) readings, at most `UPLOAD_MAX_BATCHES_PER_WAKE` (8) posts per wake (both "Not set by spec", in `config.h` and `CycleConfig`). Each post's buffered readings are dropped from flash only after the server took it. A failed post stops the wake: what is left stays in order, and the new reading is buffered behind it. A long backlog drains over several wakes so a wake stays inside the upload watchdog. When the buffer holds `bufferCapacity` readings, the oldest go first (`WakeResult::bufferDropped`, logged with `log_w`). `wifi` test readings are never buffered.
- **No duplicates:** the uplink posts to `/rest/v1/readings?on_conflict=device_id,seq,boot_count` with `Prefer: return=minimal,resolution=ignore-duplicates`. PostgREST turns that into `INSERT ... ON CONFLICT (device_id, seq, boot_count) DO NOTHING`, which needs only the insert privilege and the insert policy that `schema.sql` already grants (no update privilege, unchanged). So the core may send a reading twice (a post whose reply was lost, or whose drop from flash failed: it stops there and the readings go again later) and it is still stored once. No `schema.sql` change was needed; the unique key `readings_device_seq_boot_key` was already there.
- **Storage (`Esp32Store`):** 3000 readings × `sizeof(Reading)` (about 70 bytes) is about 210 KB, far past the ~20 KB NVS partition. The buffer is a ring of raw `Reading` records in a LittleFS file (`/<ns>.buf`) with a small header file (`/<ns>.bhd`: magic, layout version, record size, capacity, head, count) on the default partition table's data partition (label `spiffs`, ~1.4 MB), so no custom partition table was needed and the app partition keeps its 1.25 MB (build: 80.8 % used). Why LittleFS rather than SPIFFS: it is power-loss safe (a file is committed only on close, so a cut leaves the old header) and wear-levels; both are in the Arduino core, no new library. A push writes the record, then the header; a drop writes only the header. A header from another layout (`BUFFER_LAYOUT_VERSION`, record size or capacity changed) starts the buffer again, logged. `LittleFS.begin(true)` formats the partition on first use. Alerts, baseline, calibration and boot count stay in NVS.
- **`clearbuf`** (`Command::ClearBuf`): empties the buffer, prints how many went and `@clearbuf ok=<0|1> cleared=<n> count=<n>` (ok=0: a flash error left `count`). No WiFi.
- **`status`** prints the buffer size in its human lines and a new `@buffer count=<n> sent=0 dropped=0` line after `@status`. Every wake prints `@buffer count=<n> sent=<n> dropped=<n>` after `@upload` (left in flash, sent this wake, dropped because full). `@upload ok=0` now means "kept in the buffer" for a wake reading.
- **Sensor failure:** when a sample read fails, the measurement resets the I2C bus once (`Imu::resetBus`) and carries on; if it fails again, or the reset fails, the reading gets `event=sensor_error` (and `stable=false`), keeping any windows taken before the failure. It is stamped, uploaded or buffered like any other reading, with the usual health fields from the uplink, and with no still window it doesn't change the level. `Esp32Imu::resetBus` clocks SCL up to 9 times while SDA is held low, sends a STOP, then sets the MPU6050 up again (a few hundred µs, pins from `config.h`). The human reading line adds "SENSOR ERROR ...".
- **Device config:** `main.cpp` now runs everything with `deviceCycleConfig()` (`CycleConfig` defaults plus `bufferCapacity = BUFFER_CAPACITY`); see Open questions for why the default is 0.
- **Tests:** `native/test_offline_buffer` (13: buffered when down or refused, sent in order and emptied, identity and time kept, full buffer drops the oldest, failure part way loses and duplicates nothing, a long backlog over several wakes, a failed drop resends later, the device config, `clearbuf` and its flash error, `status` count, the two lines). `native/test_sensor_error` (6: no answer gives `sensor_error`, level unchanged, buffered offline, recovery after a reset gives a normal reading, a later failure after a reset that worked, a working sensor is never reset). `embedded/test_buffer_reboot` (survives a reboot in order, drop then push, clear). 139 native tests pass (before merging #9).
- **Not done:** hourly batching, dating back buffered readings from before the first sync, and skipping a row Supabase always refuses (all in Open questions). No board test (pytest) for the buffer: the outage check is manual, in "Still to verify".

## 2026-10-17: #9 Minimal dashboard

A static page in `dashboard/` (`index.html`, `style.css`, small ES modules in `js/`) that reads the `real_readings` view for `deviceId`, newest 1000 rows, with the Supabase JS client 2.45.4 and Chart.js 4.4.1 from CDNs (exact versions, SRI hashes). It shows the status card, the tilt chart (pitch, roll, combined, dashed WATCH/ALERT lines), the temperature chart, the events list and the OFFLINE badge. Phone-first, light and dark mode, web manifest plus apple-touch icon and meta tags for the home screen. No login, no service worker. Refreshes every 5 min and when the page comes back to the front. `?demo` and `?demo=offline` show made-up data without Supabase. Photos are left for #10.

- **Config:** `dashboard/config.js` (git-ignored, template `config.example.js`): Supabase URL, public key, device id, thresholds, OFFLINE limit (120 min), rows to load, refresh interval. Without it the page says how to create it.
- **Tests:** `node --test dashboard/test/*.test.js` (Node's built-in runner, nothing to install), 36 tests, one skipped until `config.js` exists. They cover `js/readings.js` (status/OFFLINE, events, chart series), `js/format.js`, the demo data, `scripts/make-config.js`, and `test/thresholds.test.js`, which reads `WATCH_DEG`/`ALERT_DEG` out of `include/config.h` and fails if `config.example.js` (or `config.js`) differs. CI runs them on Node 20 (new step in `ci.yml`). Node isn't installed on the dev PC: locally they were run with VS Code's bundled Node 24 (`ELECTRON_RUN_AS_NODE=1 Code.exe --test ...`).
- **Checked locally:** served with `python -m http.server` and loaded in headless Chrome at phone width: demo data, demo offline, and the real Supabase code path against a fake PostgREST response (local mock, then removed). All rendered with no console errors. Not yet tried on real data or a real phone (see "Still to verify").
- **Publishing:** `.github/workflows/dashboard-pages.yml` builds `config.js` from repo variables `SUPABASE_URL`, `DEVICE_ID` and secret `SUPABASE_PUBLIC_KEY`, and deploys to GitHub Pages. It runs only when started by hand (`workflow_dispatch`), so it can't run, or turn anything red, before the user sets it up. `make-config.js` refuses a key that isn't public (`sb_secret_...` or a JWT whose role isn't `anon`). Nothing was enabled or published.

Decisions made without the user (safest option, open to change):

- **Rows with an unset clock are left out** (`ts` before 2023-11-14, `config::CLOCK_VALID_AFTER_UTC`): they'd plot at 1970. Rows with `time_synced=false` but a back-dated real `ts` are kept.
- **OFFLINE uses the newest reading's `ts`**, not `received_at`, as the issue's acceptance criterion says. Since uploads are hourly, the badge can appear up to an hour later than the 2 h, in the worst case.
- **The first loaded reading is never a level change** (what came before isn't loaded), so a WATCH that began more than 1000 readings ago shows on the card but not in the events.
- **Threshold lines only at +WATCH and +ALERT.** Levels apply to the combined tilt delta, which is never negative; pitch and roll can go negative, but no line is drawn below zero.
- **Thresholds live in `config.js`** (as the ticket asks), checked against `config.h` by a test, rather than generated from it.
- **CSP meta tag:** scripts only from the page itself and the two CDNs, data only from `https://*.supabase.co`. A custom Supabase domain would need adding to `connect-src` in `index.html`.
- **Icons** are drawn by a throwaway script (not committed): a green square with a slope and a leaning post.

Open question: the public key the page uses can also **insert** readings (the device uses the same key, `schema.sql`), so anyone who reads it from the page could add fake rows. That was already true of the device key; fixing it would need a separate device key or a check in the database.

## 2026-10-16: #22 Six-position accelerometer calibration

The board run found the MPU6050 reading 0.876 g at rest. Against the baseline an offset mostly cancels out, but a scale error doesn't: with Z 12 % low, a real 2° tilt reads about 2.3°. The user chose to calibrate before merging `core-build`.

- **Commands:** `cal +x|-x|+y|-y|+z|-z` (that axis of the MPU6050 pointing up, board still) averages the raw samples for as long as a baseline (`BASELINE_WINDOWS`, `BASELINE_TIME_LIMIT_MS`). Faces go in any order and can be retaken; they are kept in `RtcState::cal`, so a power loss means taking them again. A capture is refused if the board moves (`err=moving`) or another axis is the largest, or the named one has the wrong sign (`err=wrong_face`). `cal save` needs all six (`err=missing_faces missing=-x,+z`). Per axis, offset = (up + down) / 2 and scale = (up − down) / 2. Each must be inside `CAL_SCALE_MIN`–`CAL_SCALE_MAX` (0.7–1.3) and `|offset|` ≤ `CAL_OFFSET_MAX_G` (0.25 g) (`err=out_of_range`); all three are "Not set by spec". Then it is saved to flash (NVS key `accelcal`).
- **Baseline (user decision):** `cal save` and `cal clear` erase the baseline, because it was measured with the old correction. They also restart the step rule (as `baseline` does). The serial line asks the installer to mount the board and run `baseline`; otherwise the next wake captures one wherever the board is.
- **Correction:** every reading (wake, `read`, `wifi`, `baseline`, first-boot baseline) corrects each sample as (raw − offset) / scale before the motion gate and the angles. With no calibration stored, nothing changes. `pitch_raw`/`roll_raw` are still "before temperature compensation" (#11), now after this correction. Supabase has no column saying whether a reading was corrected (see Open questions).
- **Replies:** new `@cal ok= step= faces= [ax= ay= az=] [ox= oy= oz= sx= sy= sz=] [err=] [missing=] [baseline=0]`. `@read` and `@status` gain `cal=0|1` at the end. This changed the expected strings of three existing tests in `native/test_read_reply` (only ` cal=0` appended), approved by the user. `status` prints the stored offset and scale in its human-readable lines.
- **Tests:** PC suite `native/test_accel_calibration` (24 tests: maths, face checks, each refusal, the correction in wake, read and baseline, raw captures with a calibration already stored, the reply lines). Manual board test `test/hil/board/test_calibration.py` (`--manual`, normal `esp32dev` build): six faces, `cal save`, then a flat `read` must give `cal=1` and 1 ± 0.02 g, and a new baseline reads 0 ± 0.25°. Self-check `selfcheck/test_calibration_selfcheck.py` with `fake_cal_firmware.py` (a fake sensor with offset and scale errors): passes on a healthy board and FAILS for a silent port, no `cal`, a refused capture, a face count that doesn't go up, a reply for another face, a refused save, a kept baseline, no correction, `cal=1` with no correction, a correction reported as `cal=0`, a total 0.03 g off, the last face left on its side, a refused or unsubtracted new baseline, and an unstable read (17 tests, 150 in the self-check).
- `embedded/test_imu` still checks the raw sensor for 1 g, unchanged (user decision), so it keeps failing on this module. The calibrated check is the board reading test `test_still_board_reads_about_1g`.

Also in this pass: `test_upload.py`'s `FIRST_REPLY_WITHIN_S` now comes from `harness.upload_worst_case_s()` (same 120 s), with the user's approval.

## 2026-10-16: Code review fixes (spec #14)

One pass over the `core-build` code review. Behaviour fixes were built test-first at seam 2 (the wake cycle on the PC); **no existing test was changed**. New suites: `native/test_alert_queue` (5 tests), `native/test_command_effects` (2), `native/test_sleep_timer` (1). New fake members: the alert queue in `FakeStore` (it mirrors its oldest entry into the old `hasPendingAlert` / `pendingAlert` fields, so the #20 tests still see it) and `alertDropFails`.

**Behaviour:**

- **Alerts are queued, not overwritten.** A newer raise used to replace an alert still waiting for ntfy (an ALERT, then a drop, then a WATCH lost the ALERT). Now raised alerts go into a flash queue (`Store::alertPush/alertPeek/alertCount/alertReplaceNewest/alertDrop`; `Esp32Store` keeps it as one NVS blob, key `alerts`) and are sent oldest first, stopping at the first failure so the order holds. `config::ALERT_QUEUE_LEN` = 8 (not set by spec); when full, the oldest is dropped and `main.cpp` logs a `log_w` (`WakeResult::alertsDropped`). A pending alert left in the old `alert` key joins the queue. The old single-slot `Store` methods remain as defaults over the queue.
  - **Decision: an escalation still waiting is folded.** WATCH then ALERT before either went out sends one ALERT (the newest entry is replaced). This keeps the existing #20 test `test_a_newer_alert_replaces_one_still_waiting`. Same or lower levels are never folded, so two ALERTs with a drop between, or an ALERT then a WATCH, are both delivered.
  - **Send OK, clear failed:** the delivered alerts are counted in `RtcState::alertsUncleared`, cleared on a later wake and never sent again. That count is in RTC memory, so after a power loss in that rare case those alerts are sent once more: **ntfy delivery is at-least-once** there (exactly once otherwise).
  - `WakeResult::alertsSent` counts what went out; `alertSent` means every waiting alert went; `alert` is the last one tried (one `@alert` line per wake, as before).
- **`baseline` restarts the step rule.** After a new baseline, `LevelState::hasPrevious` is reset, so the old delta can't raise a false 0.5° step ALERT on the next wake.
- **The `wifi` reading carries the current level** (`state.levels.level`) into its reply and its Supabase row, instead of always OK. A command reading still doesn't change the level.
- **1 s minimum sleep in the core.** `runWakeCycle` never asks for a 0 s timer; the rule moved out of `Esp32Sleeper`.
- **`time_synced` before the first sync:** done after the review, on the user's decision (see Open questions).

**Tidy-ups (behaviour unchanged):** one `httpsPost()` helper in `Esp32Ports.cpp` for Supabase and ntfy (same CA check, timeouts and log text); `READING_JSON_BUF_SIZE` (640) in `ReadingJson.h` for the row buffer and body reserve; `isTestTag()` for every "is this a test" check, one `tag=` formatter in `TaggedReply.cpp`, one `wifi`/`alert` argument parser in `main.cpp`; `withPrefs()`/`getBlob()`/`putBlob()` for the `Preferences` begin/put/end pattern in `Esp32Store`; `isUtcValid()` in `Ports.h` shared by `Esp32Clock::isSynced` and the alert message; the ESP32 `pollConnect`/`pollSync` call `vTaskDelay(1)` while Pending, so the blocking `connect()`/`sync()` wrappers (documented as `setup()` and `runCommand` only) don't hog the CPU; `WifiCommand::command()` replaces `main.cpp`'s `netCommand` copy (the class keeps its name; a comment says it also runs `alert`).

## 2026-10-16: #20 Bare levels and ntfy alerts

- **Levels (core, `classify()` in `lib/WakeCycle/src/WakeCycle.cpp`):** every wake reading is classified on the device and the level goes into the reading (`Reading::level`), its Supabase row (`level`, already in `ReadingJson` and `schema.sql`) and `WakeResult::level`. The rules, on the tilt delta, with thresholds in `config.h` (copied into `CycleConfig` so tests can change them):
  - **WATCH:** tilt ≥ `WATCH_DEG` (now **1.0°**, was 0.5°; ADR 0002) on `WATCH_CONFIRM_READINGS` (2) readings in a row.
  - **ALERT:** tilt ≥ `ALERT_DEG` (2°), or a **step** ≥ `ALERT_STEP_DEG` (0.5°) since the previous wake reading. Immediate.
  - **Margin:** a level is held until the tilt is `LEVEL_DROP_MARGIN_DEG` (0.1°) below its threshold: WATCH holds down to 0.9°, ALERT down to 1.9°. Then it falls to whatever the readings support (ALERT can drop straight to WATCH or OK). A step ALERT with a small tilt drops on the next reading without a step.
  - The rules' memory (level, WATCH count, previous pitch/roll) is `RtcState::levels`, kept through deep sleep and reset on a cold boot.
- **Alerts:** a notification only when the level goes up (OK→WATCH, OK→ALERT, WATCH→ALERT). It is saved to flash (`Store::savePendingAlert`) *before* WiFi, then sent in the same connection as the upload (alert first), and cleared only when ntfy accepted it. Every wake retries a pending alert until delivered, so it goes out exactly once. Each attempt prints `@alert ok=<0|1> tag=none level= tilt=`. PC suite: `native/test_levels` (19 tests).
- **ntfy (`Esp32Uplink::sendAlert`):** HTTPS POST to `config::NTFY_SERVER` (`https://ntfy.sh`) + `/` + `secrets::NTFY_TOPIC`, certificate checked with `setCACert(root_ca::HTTPS_ROOTS)` (ISRG Root X1 for ntfy.sh; no `setInsecure()`), 10 s timeouts. Headers: `Title: Landslide monitor <device>: <level|TEST>`, `Priority` 5 for ALERT, 4 for WATCH, 3 for tests, `Tags` warning / test_tube. The body (`lib/WakeCycle/src/AlertMessage.*`, PC-tested) gives the level, the tilt delta and the time (ISO UTC, or "Time unknown (n s after power-on)" before the first sync); an alert raised before the first sync is dated back with its reading.
- **Pending alert in flash (`Esp32Store`):** the `Alert` struct as one NVS blob (key `alert`), removed on delivery.
- **Test-alert command:** `alert <n>` sends a ntfy message whose first line is **"TEST — not a real alert"**, with "Test number <n>", the device, its current level and the latest tilt. Plain `alert` picks a random 9-digit number (like `wifi`). It prints `@alert ok=<0|1> tag=<n> level=<current> tilt=<latest>`. It runs on the #19 `WifiCommand` state machine (now `start(Command::Alert, n, ports)`: join, NTP, send, radio off), polled from `loop()` with `CommandWindow::commandStarted()` keeping the board awake, so `loop()` never blocks. A failed test alert is reported, never saved for retry, and it leaves a pending real alert and the level alone. PC suite: `native/test_alert_command` (9 tests, including the message text).
- **Board test (`test/hil/board/test_alert.py`):** `alert <random>` must give `@alert ok=1 tag=<random>`, then ntfy's JSON poll (`/<topic>/json?poll=1&since=<test start − 5 s>`) must hold exactly one message whose first line is the disclaimer and which contains the number, dated after the test start (5 s clock skew) and not in the future. It polls up to 10 times, 1 s apart. The PC reads `NTFY_TOPIC` from `include/secrets.h` and `NTFY_SERVER` from `config.h` (`test/hil/ntfy_rest.py`); the example topic makes the test FAIL, never skip.
- **Self-check (`selfcheck/test_alert_selfcheck.py`, `fake_ntfy.py`):** passes against a healthy fake board and topic (also with other messages on it), and FAILS for: no message, only a stale message, a message dated before the test or in the future, another number, no disclaimer, the message twice, `@alert ok=0`, a silent port, and a board without `alert`. The fake topic ignores `since=`, so the test's own date check is what is proven. Also checks the disclaimer matches the firmware's and the secrets parsing against the template. 17 tests (129 in the self-check). Its time limits come from `config.h` through `harness.upload_worst_case_s()`: first reply 45 + 85 + 20 (a pending alert's post) = **150 s**, `alert` reply 85 + 15 = **100 s**.
- **Decisions to review:**
  - **Step = movement in any direction:** the step is the distance between this and the previous reading's (pitch, roll) deltas, not the change in tilt size. So 0.3° one way to 0.3° the other way (0.6° of movement, the same tilt) is an ALERT.
  - **The first reading after a cold boot has no step** (there is no previous reading), and the level starts at OK after a cold boot. A slope still tilted past 2° alerts again after a power loss.
  - **A reading with no still window** (the board moved the whole time) changes nothing: no angle was measured, and treating it as 0° would look like the baseline moving. A partly still reading (`stable=0` but some windows accepted) is classified.
  - **One pending slot:** a newer alert replaces an unsent one (WATCH waiting, then ALERT: only ALERT is sent).
  - `read` and `wifi` readings are not classified and don't change the level; only wake readings do. A `wifi` test row carries level OK.
  - The test alert syncs the time first, so its message has a real time.
- **Not done (deferred by #14):** event mode and the CAM pulse.

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
  - **Dating back:** a reading taken before the first sync (the first wake after power-on) gets real time once the sync succeeds in the same wake: now minus the uptime since it was taken, and `time_synced=true`. Only a reading whose wake doesn't sync stays `time_synced=false` with time since power-on. *(Superseded 2026-10-16: the reading is still dated back but stays `time_synced=false`.)*
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
  - **First-reply limits (user-approved change, #14 2026-10-16).** The power-on wake also uploads before the command window opens, so with no network in range it spends up to 85 s on WiFi, NTP and HTTPS first. `harness.upload_worst_case_s()` works that out from `config.h` (2 × 30 s + 5 s + 2 × 10 s = **85 s**) and is added to `FIRST_CYCLE_S` (35 → **120 s**) and `FIRST_REPLY_WITHIN_S` (45 → **130 s**) in `test_sleep.py`, `PONG_WITHIN_S` in `test_hello.py` and `FIRST_REPLY_WITHIN_S` in `test_reading.py` (45 → **130 s**). Two self-check tests were added (112 in all).
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
