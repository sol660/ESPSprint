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
};

struct CycleConfig {
  uint32_t normalIntervalS = config::NORMAL_INTERVAL_S;
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
