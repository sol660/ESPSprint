#pragma once

// Fake hardware for PC tests of the wake cycle. Each fake records what the
// core asked of it and returns responses the test scripts in advance.

#include <deque>
#include <vector>

#include "Ports.h"

class FakeImu : public Imu {
 public:
  std::deque<ImuSample> samples;  // scripted; readSample fails when empty
  float temperatureC = 20.0f;
  bool responding = true;
  std::vector<float> motionThresholds;
  int busResets = 0;

  bool readSample(ImuSample& out) override {
    if (!responding || samples.empty()) return false;
    out = samples.front();
    samples.pop_front();
    return true;
  }
  bool readTemperatureC(float& out) override {
    if (!responding) return false;
    out = temperatureC;
    return true;
  }
  bool setMotionThreshold(float g) override {
    motionThresholds.push_back(g);
    return responding;
  }
  bool resetBus() override {
    ++busResets;
    return responding;
  }
};

class FakeClock : public Clock {
 public:
  uint32_t utc = 1792800000;  // 2026-10-24
  bool synced = true;
  bool syncSucceeds = true;
  uint32_t uptime = 0;
  uint32_t msPerUptimeRead = 0;  // advance time on each read, to drive time limits
  int syncCalls = 0;

  uint32_t nowUtc() override { return utc; }
  bool isSynced() override { return synced; }
  uint32_t uptimeMs() override {
    uint32_t now = uptime;
    uptime += msPerUptimeRead;
    return now;
  }
  bool sync() override {
    ++syncCalls;
    if (syncSucceeds) synced = true;
    return syncSucceeds;
  }
};

class FakeStore : public Store {
 public:
  bool hasBaseline = false;
  Baseline baseline{};
  int baselineSaves = 0;
  bool hasCalibration = false;
  TempCalibration calibration{};
  std::deque<Reading> buffer;
  bool hasPendingAlert = false;
  Alert pendingAlert{};

  bool loadBaseline(Baseline& out) override {
    if (hasBaseline) out = baseline;
    return hasBaseline;
  }
  bool saveBaseline(const Baseline& b) override {
    baseline = b;
    hasBaseline = true;
    ++baselineSaves;
    return true;
  }
  bool loadCalibration(TempCalibration& out) override {
    if (hasCalibration) out = calibration;
    return hasCalibration;
  }
  bool bufferPush(const Reading& r) override {
    buffer.push_back(r);
    return true;
  }
  size_t bufferCount() override { return buffer.size(); }
  bool bufferPeek(size_t index, Reading& out) override {
    if (index >= buffer.size()) return false;
    out = buffer[index];
    return true;
  }
  void bufferDrop(size_t count) override {
    while (count-- > 0 && !buffer.empty()) buffer.pop_front();
  }
  bool loadPendingAlert(Alert& out) override {
    if (hasPendingAlert) out = pendingAlert;
    return hasPendingAlert;
  }
  bool savePendingAlert(const Alert& a) override {
    pendingAlert = a;
    hasPendingAlert = true;
    return true;
  }
  void clearPendingAlert() override { hasPendingAlert = false; }
};

class FakeUplink : public Uplink {
 public:
  bool up = true;
  std::vector<std::vector<Reading>> batchesSent;
  std::vector<Alert> alertsSent;

  bool sendReadings(const Reading* readings, size_t count) override {
    if (!up) return false;
    batchesSent.emplace_back(readings, readings + count);
    return true;
  }
  bool sendAlert(const Alert& alert) override {
    if (!up) return false;
    alertsSent.push_back(alert);
    return true;
  }
};

class FakeCamTrigger : public CamTrigger {
 public:
  int pulses = 0;
  void pulse() override { ++pulses; }
};

class FakeSleeper : public Sleeper {
 public:
  struct Call {
    uint32_t timerS;
    bool motionWake;
  };
  std::vector<Call> calls;
  void sleep(uint32_t timerS, bool motionWake) override { calls.push_back({timerS, motionWake}); }
};

struct FakeHardware {
  FakeImu imu;
  FakeClock clock;
  FakeStore store;
  FakeUplink uplink;
  FakeCamTrigger cam;
  FakeSleeper sleeper;
  Ports ports_{imu, clock, store, uplink, cam, sleeper};

  Ports& ports() { return ports_; }
};
