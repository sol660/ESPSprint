#pragma once

// One wake of the node, free of hardware: setup() builds the real Ports, calls
// runWakeCycle() once and then acts on the result (sleeps as it says).

#include "Ports.h"
#include "config.h"

enum class WakeCause : uint8_t { PowerOn, Timer, Motion, Serial };

// Kept across deep sleep in RTC memory.
struct RtcState {
  uint32_t seq = 0;        // readings numbered since the last cold boot
  uint32_t bootCount = 0;  // this cold boot's number (kept in flash, cached here)
  Reading lastReading{};
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
  bool sleepDue(uint32_t nowMs) const;

 private:
  uint32_t fromMs_;
  uint32_t forMs_;
  uint32_t idleMs_;
};

struct WakeResult {
  bool tookReading = false;
  Reading reading{};
  Level level = Level::Ok;
  size_t readingsSent = 0;
  bool uploadTried = false;  // print `@upload ok=` when set
  bool uploaded = false;     // false: the reading was dropped (no buffer yet)
  bool alertSent = false;
  bool camPulsed = false;
  uint32_t nextWakeS = 0;
  bool motionWake = false;
};

WakeResult runWakeCycle(WakeCause cause, RtcState& state, const CycleConfig& cfg, Ports& ports);

// Installer commands typed over USB serial. The serial adapter parses and prints;
// this does the work.
// Wifi: connect, sync the time, take a reading and upload it tagged with testTag
// (Supabase test_tag), so the installer and the board test can find that row.
// No other command uses WiFi.
enum class Command : uint8_t { Read, Baseline, Status, Wifi };

struct CommandResult {
  bool ok = false;
  Reading reading{};
  bool hasBaseline = false;
  Baseline baseline{};
  bool uploadTried = false;
  bool uploaded = false;
};

// testTag is used by Command::Wifi only (non-zero).
CommandResult runCommand(Command cmd, RtcState& state, const CycleConfig& cfg, Ports& ports,
                         uint32_t testTag = 0);
