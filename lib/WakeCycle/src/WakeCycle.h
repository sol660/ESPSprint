#pragma once

// One wake of the node, free of hardware: setup() builds the real Ports, calls
// runWakeCycle() once and then acts on the result (sleeps as it says).

#include "Ports.h"
#include "config.h"

enum class WakeCause : uint8_t { PowerOn, Timer, Motion, Serial };

// Kept across deep sleep in RTC memory.
struct RtcState {
  uint32_t seq = 0;
  uint32_t bootCount = 0;
  Reading lastReading{};
};

struct CycleConfig {
  uint32_t normalIntervalS = config::NORMAL_INTERVAL_S;
  uint16_t windowSamples = config::WINDOW_SAMPLES;
  uint8_t windowsPerReading = config::WINDOWS_PER_READING;
  float stillAccelStdG = config::STILL_ACCEL_STD_G;
  float stillGyroStdDps = config::STILL_GYRO_DPS;
  uint32_t readingTimeLimitMs = config::READING_TIME_LIMIT_MS;
  uint8_t baselineWindows = config::BASELINE_WINDOWS;
  uint32_t baselineTimeLimitMs = config::BASELINE_TIME_LIMIT_MS;
};

struct WakeResult {
  bool tookReading = false;
  Reading reading{};
  Level level = Level::Ok;
  size_t readingsSent = 0;
  bool alertSent = false;
  bool camPulsed = false;
  uint32_t nextWakeS = 0;
  bool motionWake = false;
};

WakeResult runWakeCycle(WakeCause cause, RtcState& state, const CycleConfig& cfg, Ports& ports);

// Installer commands typed over USB serial. The serial adapter parses and prints;
// this does the work.
enum class Command : uint8_t { Read, Baseline, Status };

struct CommandResult {
  bool ok = false;
  Reading reading{};
  bool hasBaseline = false;
  Baseline baseline{};
};

CommandResult runCommand(Command cmd, RtcState& state, const CycleConfig& cfg, Ports& ports);
