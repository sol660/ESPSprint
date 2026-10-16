#pragma once

// Hardware interfaces the wake-cycle core talks through. Real versions live in
// lib/Esp32Ports; PC tests use fakes. No Arduino includes here.

#include <cstddef>
#include <cstdint>

struct ImuSample {
  float ax, ay, az;  // g
  float gx, gy, gz;  // deg/s
};

struct Baseline {
  float pitchDeg;
  float rollDeg;
};

// Per-axis straight-line offset against temperature; all zero = no correction.
struct TempCalibration {
  float pitchPerC, pitchAt0C;
  float rollPerC, rollAt0C;
};

enum class Level : uint8_t { Ok, Watch, Alert };
enum class Trigger : uint8_t { Scheduled, Motion, Recheck, Event };
enum class ReadingEvent : uint8_t { None, Shock, SensorError };

struct Reading {
  uint32_t seq;
  uint32_t bootCount;
  uint32_t ts;  // UTC seconds
  bool timeSynced;
  Trigger trigger;
  float pitchDeg, rollDeg, tiltDeg;  // compensated tilt delta
  float pitchRaw, rollRaw;           // averaged angle before compensation
  float tempC;
  float noise;  // mean accelerometer std dev of the accepted windows, g
  float accelG;  // magnitude of the averaged acceleration, g (about 1 at rest)
  bool stable;
  Level level;
  ReadingEvent event;
  uint32_t testTag;  // 0 = a real reading; else the `wifi` command's tag (Supabase test_tag)
};

// A notification that the level went up (or a test message: testTag != 0).
struct Alert {
  Level level;     // the new level (for a test: the level the device is at)
  float tiltDeg;   // the tilt delta that raised it
  uint32_t ts;     // UTC seconds of the reading; before the first time sync, time since power-on
  uint32_t testTag = 0;  // 0 = a real alert; else the `alert` command's number
};

class Imu {
 public:
  virtual ~Imu() = default;
  virtual bool readSample(ImuSample& out) = 0;
  virtual bool readTemperatureC(float& out) = 0;
  virtual bool setMotionThreshold(float g) = 0;
  virtual bool resetBus() = 0;
};

// How a slow network step is going, for steps that are started and then polled
// from loop() (CLAUDE.md: no blocking waits in loop()).
enum class Progress : uint8_t { Pending, Done, Failed };

class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t nowUtc() = 0;
  virtual bool isSynced() = 0;
  virtual uint32_t uptimeMs() = 0;  // for in-wake time budgets
  // Time sync over the network (needs the Uplink connected), in two steps:
  // startSync() returns at once; pollSync() checks without waiting and gives
  // Pending until the time is set (Done) or its time limit passes (Failed).
  virtual void startSync() = 0;
  virtual Progress pollSync() = 0;
  // The same, waiting until it finishes. For the wake cycle in setup() only.
  virtual bool sync() {
    startSync();
    Progress p;
    while ((p = pollSync()) == Progress::Pending) {
    }
    return p == Progress::Done;
  }
};

class Store {
 public:
  virtual ~Store() = default;
  virtual bool loadBaseline(Baseline& out) = 0;
  virtual bool saveBaseline(const Baseline& b) = 0;
  virtual bool loadCalibration(TempCalibration& out) = 0;
  // Cold boots so far; false when none has been saved yet.
  virtual bool loadBootCount(uint32_t& out) = 0;
  virtual bool saveBootCount(uint32_t count) = 0;
  virtual bool bufferPush(const Reading& r) = 0;
  virtual size_t bufferCount() = 0;
  virtual bool bufferPeek(size_t index, Reading& out) = 0;  // 0 = oldest
  virtual void bufferDrop(size_t count) = 0;                // drops the oldest
  virtual bool loadPendingAlert(Alert& out) = 0;
  virtual bool savePendingAlert(const Alert& a) = 0;
  virtual void clearPendingAlert() = 0;
};

// WiFi + Supabase + ntfy in v1; replaceable (own WiFi code, ESP-NOW, cellular).
class Uplink {
 public:
  virtual ~Uplink() = default;
  // Joining a network, in two steps: startConnect() returns at once;
  // pollConnect() checks without waiting and gives Pending until a network is
  // joined (Done) or every network has had its time limit (Failed = offline for
  // this wake, a normal result, not an error).
  virtual void startConnect() = 0;
  virtual Progress pollConnect() = 0;
  // The same, waiting until it finishes. For the wake cycle in setup() only.
  virtual bool connect() {
    startConnect();
    Progress p;
    while ((p = pollConnect()) == Progress::Pending) {
    }
    return p == Progress::Done;
  }
  // Radio off. Called after every upload attempt, connected or not.
  virtual void disconnect() = 0;
  // Needs connect() first. True only if the server stored every reading.
  virtual bool sendReadings(const Reading* readings, size_t count) = 0;
  virtual bool sendAlert(const Alert& alert) = 0;
};

class CamTrigger {
 public:
  virtual ~CamTrigger() = default;
  virtual void pulse() = 0;
};

class Sleeper {
 public:
  virtual ~Sleeper() = default;
  virtual void sleep(uint32_t timerS, bool motionWake) = 0;
};

struct Ports {
  Imu& imu;
  Clock& clock;
  Store& store;
  Uplink& uplink;
  CamTrigger& cam;
  Sleeper& sleeper;
};
