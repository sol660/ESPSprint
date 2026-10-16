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
  uint32_t utcAfterSync = 0;  // if set, a successful sync sets the clock to this
  bool sync() override {
    ++syncCalls;
    if (syncSucceeds) {
      synced = true;
      if (utcAfterSync != 0) utc = utcAfterSync;
    }
    return syncSucceeds;
  }
  // Step by step: startSync() counts a sync, then pollSync() is Pending this many times.
  int pollsBeforeSynced = 0;
  int syncPollsLeft = 0;
  void startSync() override {
    ++syncCalls;
    syncPollsLeft = pollsBeforeSynced;
  }
  Progress pollSync() override {
    if (syncPollsLeft > 0) {
      --syncPollsLeft;
      return Progress::Pending;
    }
    if (!syncSucceeds) return Progress::Failed;
    synced = true;
    if (utcAfterSync != 0) utc = utcAfterSync;
    return Progress::Done;
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
  bool hasBootCount = false;
  uint32_t bootCount = 0;
  int bootCountSaves = 0;

  bool loadBootCount(uint32_t& out) override {
    if (hasBootCount) out = bootCount;
    return hasBootCount;
  }
  bool saveBootCount(uint32_t count) override {
    bootCount = count;
    hasBootCount = true;
    ++bootCountSaves;
    return true;
  }

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
  bool up = true;                // the network can be joined
  bool acceptsReadings = true;   // the server stores what is posted
  bool connected = false;
  int connects = 0;
  int disconnects = 0;
  std::vector<std::vector<Reading>> batchesSent;
  std::vector<Alert> alertsSent;

  bool connect() override {
    ++connects;
    connected = up;
    return connected;
  }
  // Step by step: startConnect() counts a connect, then pollConnect() is Pending
  // this many times before it joins (or fails, when !up).
  int pollsBeforeJoined = 0;
  int joinPollsLeft = 0;
  void startConnect() override {
    ++connects;
    connected = false;
    joinPollsLeft = pollsBeforeJoined;
  }
  Progress pollConnect() override {
    if (joinPollsLeft > 0) {
      --joinPollsLeft;
      return Progress::Pending;
    }
    connected = up;
    return up ? Progress::Done : Progress::Failed;
  }
  void disconnect() override {
    ++disconnects;
    connected = false;
  }
  bool sendReadings(const Reading* readings, size_t count) override {
    if (!connected || !acceptsReadings) return false;
    batchesSent.emplace_back(readings, readings + count);
    return true;
  }
  bool acceptsAlerts = true;  // ntfy takes what is posted
  int alertAttempts = 0;
  bool sendAlert(const Alert& alert) override {
    ++alertAttempts;
    if (!up || !acceptsAlerts) return false;
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
