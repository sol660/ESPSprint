#pragma once

// One wake of the node, free of hardware: setup() builds the real Ports, calls
// runWakeCycle() once and then acts on the result (sleeps as it says).

#include "Ports.h"
#include "config.h"

enum class WakeCause : uint8_t { PowerOn, Timer, Motion, Serial };

// The level rules' memory between wake-cycle readings (spec #1 "Levels").
struct LevelState {
  Level level = Level::Ok;
  uint8_t watchStreak = 0;  // wake readings in a row at or over WATCH
  bool hasPrevious = false;  // false until the first classified reading of this cold boot
  float prevPitchDeg = 0;    // the previous classified reading's tilt delta, for the step rule
  float prevRollDeg = 0;
};

// The six faces of the accelerometer calibration (#22): which axis points up.
// Named +x, -x, +y, -y, +z, -z after the MPU6050's printed axis arrows.
enum class CalFace : uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };
constexpr int CAL_FACE_COUNT = 6;

// Face averages taken so far by `cal <face>`, until `cal save` uses them.
struct CalCapture {
  uint8_t faces = 0;  // bit i set: face i captured
  float mean[CAL_FACE_COUNT][3] = {};  // raw averaged acceleration, g
};

// Kept across deep sleep in RTC memory.
struct RtcState {
  uint32_t seq = 0;        // readings numbered since the last cold boot
  uint32_t bootCount = 0;  // this cold boot's number (kept in flash, cached here)
  Reading lastReading{};
  LevelState levels{};
  // Alerts at the front of the flash queue that ntfy already took but whose
  // clear from flash failed: cleared on a later wake, never sent again. Lost
  // with RTC memory on a power loss, so those few are then sent once more
  // (ntfy delivery is at-least-once in that rare case).
  uint8_t alertsUncleared = 0;
  CalCapture cal{};  // lost on a power loss: the faces are then taken again
};

// What the boot banner (`@boot fw= boot= cause= seq=`) reports.
struct BootInfo {
  const char* fwVersion;
  uint32_t bootCount;
  WakeCause cause;
  uint32_t seq;  // readings numbered so far in this cold boot (0 after a cold boot)
};

// True for power-on, reset and crash; false for timer and motion wakes from deep sleep.
bool isColdBoot(WakeCause cause);

// Call first in every boot, before any slow work. A cold boot counts one more
// boot and restarts seq; timer and motion wakes keep both (RTC memory).
BootInfo startBoot(WakeCause cause, RtcState& state);
// The same, with the count kept in flash: a cold boot counts on from the stored
// count and saves the new one, so (device_id, seq, boot_count) never repeats
// after a power loss.
BootInfo startBoot(WakeCause cause, RtcState& state, Store& store);

// Stable name used in tagged lines: PowerOn, Timer, Motion, Serial.
const char* wakeCauseName(WakeCause cause);

struct CycleConfig {
  uint32_t normalIntervalS = config::NORMAL_INTERVAL_S;
  uint16_t windowSamples = config::WINDOW_SAMPLES;
  uint8_t windowsPerReading = config::WINDOWS_PER_READING;
  float stillAccelStdG = config::STILL_ACCEL_STD_G;
  float stillGyroStdDps = config::STILL_GYRO_DPS;
  uint32_t readingTimeLimitMs = config::READING_TIME_LIMIT_MS;
  uint8_t baselineWindows = config::BASELINE_WINDOWS;
  uint32_t baselineTimeLimitMs = config::BASELINE_TIME_LIMIT_MS;
  uint32_t commandWindowMs = config::COMMAND_WINDOW_MS;
  uint32_t sessionIdleMs = config::SERIAL_SESSION_IDLE_MS;
  float watchDeg = config::WATCH_DEG;
  uint8_t watchConfirmReadings = config::WATCH_CONFIRM_READINGS;
  float alertDeg = config::ALERT_DEG;
  float alertStepDeg = config::ALERT_STEP_DEG;
  float levelDropMarginDeg = config::LEVEL_DROP_MARGIN_DEG;
  size_t alertQueueLen = config::ALERT_QUEUE_LEN;
  float calScaleMin = config::CAL_SCALE_MIN;
  float calScaleMax = config::CAL_SCALE_MAX;
  float calOffsetMaxG = config::CAL_OFFSET_MAX_G;
};

// When an awake board may go to sleep (spec #14 "Command window and sleep").
// The serial adapter opens it once the boot's wake cycle is done, reports each
// handled command, and polls sleepDue() with millis(). Times are millis()
// values; wrap-around is handled.
class CommandWindow {
 public:
  // Power-on/reset wakes listen for cfg.commandWindowMs from nowMs; timer and
  // motion wakes don't listen, so sleep is due straight away.
  CommandWindow(WakeCause cause, uint32_t nowMs, const CycleConfig& cfg);
  // A command was handled at nowMs (call when it finishes, so a slow command
  // doesn't eat the idle time): stay awake until cfg.sessionIdleMs after it.
  void commandHandled(uint32_t nowMs);
  // A command that runs across loop() calls (`wifi`) has started: no sleep
  // until commandHandled() reports it finished.
  void commandStarted() { running_ = true; }
  bool sleepDue(uint32_t nowMs) const;

 private:
  uint32_t fromMs_;
  uint32_t forMs_;
  uint32_t idleMs_;
  bool running_ = false;
};

struct WakeResult {
  bool tookReading = false;
  Reading reading{};
  Level level = Level::Ok;
  size_t readingsSent = 0;
  bool uploadTried = false;  // print `@upload ok=` when set
  bool uploaded = false;     // false: the reading was dropped (no buffer yet)
  bool alertTried = false;  // a notification was due (new or pending): print `@alert ok=`
  bool alertSent = false;   // ntfy took every waiting one; false: the rest stay in flash for the next wake
  size_t alertsSent = 0;    // how many ntfy took this wake, oldest first
  size_t alertsDropped = 0;  // oldest alerts dropped because the queue was full (log them)
  Alert alert{};            // the last notification tried
  bool camPulsed = false;
  uint32_t nextWakeS = 0;
  bool motionWake = false;
};

WakeResult runWakeCycle(WakeCause cause, RtcState& state, const CycleConfig& cfg, Ports& ports);

// Installer commands typed over USB serial. The serial adapter parses and prints;
// this does the work.
// Wifi: connect, sync the time, take a reading and upload it tagged with testTag
// (Supabase test_tag), so the installer and the board test can find that row.
// Alert: connect, sync the time and send a ntfy test message carrying testTag
// and "TEST — not a real alert" (spec #14 story 52). No reading, no upload; a
// test that fails is reported, never kept for retry.
// No other command uses WiFi.
enum class Command : uint8_t { Read, Baseline, Status, Wifi, Alert };

struct CommandResult {
  bool ok = false;
  Reading reading{};
  bool hasBaseline = false;
  Baseline baseline{};
  bool uploadTried = false;
  bool uploaded = false;
  bool alertTried = false;  // print `@alert ok=` when set
  bool alertSent = false;
  Alert alert{};
  bool hasAccelCalibration = false;  // `status`: the calibration stored in flash
  AccelCalibration accelCalibration{};
};

// testTag is used by Command::Wifi and Command::Alert only (non-zero). Those
// two wait here until done; loop() uses WifiCommand instead, so it never blocks.
CommandResult runCommand(Command cmd, RtcState& state, const CycleConfig& cfg, Ports& ports,
                         uint32_t testTag = 0);

// The `wifi` command, polled from loop() so it never blocks on the network
// (CLAUDE.md rule). start() begins joining WiFi and returns at once; call poll()
// on every loop() until it returns true, then read result(). Steps: join WiFi,
// sync the time, then take the reading, upload it tagged with testTag and turn
// the radio off. Offline, it still reads and reports the failed upload.
// The reading and the HTTPS post themselves take a few seconds each, as in the
// `read` command; only the open-ended network waits are polled.
// The `alert` command runs the same way (start(Command::Alert, ...)): join,
// sync, then send the test message and turn the radio off.
// (The name is from #19: it runs both network commands, `wifi` and `alert`.)
class WifiCommand {
 public:
  void start(uint32_t testTag, Ports& ports) { start(Command::Wifi, testTag, ports); }
  // cmd is Command::Wifi or Command::Alert.
  void start(Command cmd, uint32_t testTag, Ports& ports);
  // True once the command has finished (also on the call that finishes it).
  bool poll(RtcState& state, const CycleConfig& cfg, Ports& ports);
  bool busy() const { return step_ == Step::Joining || step_ == Step::Syncing; }
  const CommandResult& result() const { return result_; }
  // Which command is running (or last ran): Command::Wifi or Command::Alert.
  Command command() const { return cmd_; }

 private:
  enum class Step : uint8_t { Idle, Joining, Syncing, Done };
  void finish(bool connected, RtcState& state, const CycleConfig& cfg, Ports& ports);

  Step step_ = Step::Idle;
  Command cmd_ = Command::Wifi;
  uint32_t testTag_ = 0;
  CommandResult result_{};
};

// The `cal` command (#22), six-position accelerometer calibration:
//  - `cal <face>` (Face): with that axis pointing up and the board still,
//    averages the raw samples (no stored correction) for as long as a
//    baseline and keeps the average in RtcState::cal. Refused if the board
//    moves or another axis is the one pointing up. Faces can be taken in
//    any order and retaken.
//  - `cal save` (Save): needs all six. Per axis, offset = (up + down) / 2 and
//    scale = (up - down) / 2, each checked against CycleConfig's limits, then
//    saved to flash. The baseline is erased (it was measured without the
//    correction) and the step rule starts again; the faces are forgotten.
//  - `cal clear` (Clear): erases the calibration and, for the same reason,
//    the baseline.
// Every reading afterwards corrects each sample before working out angles.
enum class CalStep : uint8_t { Face, Save, Clear };
enum class CalError : uint8_t { None, Moving, WrongFace, MissingFaces, OutOfRange, Flash };

struct CalResult {
  bool ok = false;
  CalStep step = CalStep::Face;
  CalFace face = CalFace::PosX;  // Face only
  CalError error = CalError::None;
  bool hasMean = false;  // Face: an average was measured (also on WrongFace)
  float mean[3] = {0, 0, 0};
  uint8_t faceCount = 0;    // faces captured, after this step
  uint8_t missingFaces = 0;  // Save with MissingFaces: bit i = face i not captured
  bool hasCalibration = false;  // Save: the calibration worked out (also when refused as OutOfRange)
  AccelCalibration calibration{};
  bool baselineCleared = false;
};

// `+x` ... `-z`, `save` or `clear`. face is set for a Face step only.
bool parseCalArg(const char* arg, CalStep& step, CalFace& face);
// `+x`, `-x`, `+y`, `-y`, `+z`, `-z`.
const char* calFaceName(CalFace face);
// `moving`, `wrong_face`, `missing_faces`, `out_of_range`, `flash` (`none` for None).
const char* calErrorName(CalError error);

CalResult runCalCommand(CalStep step, CalFace face, RtcState& state, const CycleConfig& cfg,
                        Ports& ports);
