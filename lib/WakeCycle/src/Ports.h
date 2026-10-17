#pragma once

// Hardware interfaces the wake-cycle core talks through. Real versions live in
// lib/Esp32Ports; PC tests use fakes. No Arduino includes here.

#include <cstddef>
#include <cstdint>

#include "config.h"

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

// Six-position accelerometer calibration (#22): per axis (x, y, z),
// corrected = (raw - offset) / scale. Not stored = no correction.
struct AccelCalibration {
  float offset[3];  // g
  float scale[3];   // 1 = no gain error
};

enum class Level : uint8_t { Ok, Watch, Alert };

// OK, WATCH or ALERT: the name in tagged lines, uploads and alerts.
inline const char* levelName(Level l) {
  switch (l) {
    case Level::Ok:
      return "OK";
    case Level::Watch:
      return "WATCH";
    case Level::Alert:
      return "ALERT";
  }
  return "OK";
}
// A Reading or Alert with a non-zero testTag comes from the `wifi` or `alert`
// command; 0 means a real one.
inline bool isTestTag(uint32_t testTag) { return testTag != 0; }

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
  bool accelCalibrated;  // the samples were corrected with a stored AccelCalibration
  // Offline buffer only (#7), not uploaded: wakes on which the server refused
  // this reading for its content. At CycleConfig::bufferMaxRefusals it is skipped.
  uint8_t uploadRefusals;
};

// A notification that the level went up (or a test message: testTag != 0).
struct Alert {
  Level level;     // the new level (for a test: the level the device is at)
  float tiltDeg;   // the tilt delta that raised it
  uint32_t ts;     // UTC seconds of the reading; before the first time sync, time since power-on
  uint32_t testTag;  // 0 = a real alert; else the `alert` command's number
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

// True once a UTC time is past config::CLOCK_VALID_AFTER_UTC. After a power loss
// the ESP32 clock counts from 1970 until the first time sync, so a smaller value
// is time since power-on, not a date.
inline bool isUtcValid(uint32_t utc) { return utc > config::CLOCK_VALID_AFTER_UTC; }

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
  // The same, waiting until it finishes (up to the time limits: WIFI_CONNECT_TIMEOUT_S
  // per network for connect, NTP_SYNC_TIMEOUT_MS for sync). Only for the wake cycle in setup() and the blocking
  // runCommand(); never from loop(). The spin doesn't hog the CPU: the real
  // pollConnect()/pollSync() give it up for a tick while Pending.
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
  virtual bool clearBaseline() = 0;  // true when none is stored afterwards
  virtual bool loadCalibration(TempCalibration& out) = 0;
  virtual bool loadAccelCalibration(AccelCalibration& out) = 0;  // false: none stored
  virtual bool saveAccelCalibration(const AccelCalibration& c) = 0;
  virtual bool clearAccelCalibration() = 0;  // true when none is stored afterwards
  // Cold boots so far; false when none has been saved yet.
  virtual bool loadBootCount(uint32_t& out) = 0;
  virtual bool saveBootCount(uint32_t count) = 0;
  virtual bool bufferPush(const Reading& r) = 0;
  virtual size_t bufferCount() = 0;
  virtual bool bufferPeek(size_t index, Reading& out) = 0;  // 0 = oldest
  virtual void bufferDrop(size_t count) = 0;                // drops the oldest
  // Rewrites the oldest buffered reading in place (its refusal count, #7).
  // False when it can't: the count is then not kept (the reading is only tried more often).
  virtual bool bufferReplaceOldest(const Reading&) { return false; }
  // Alerts waiting for ntfy, oldest first, kept in flash so a WiFi failure or
  // a crash can't swallow one. The wake cycle keeps it to
  // CycleConfig::alertQueueLen (config::ALERT_QUEUE_LEN).
  virtual size_t alertCount() = 0;
  virtual bool alertPeek(size_t index, Alert& out) = 0;  // 0 = oldest
  virtual bool alertPush(const Alert& a) = 0;            // appends after the newest
  virtual bool alertReplaceNewest(const Alert& a) = 0;
  virtual bool alertDrop(size_t count) = 0;              // drops the oldest; false on a flash error
  // The single pending alert of the first build, now the oldest queued alert.
  // The wake cycle no longer uses these.
  virtual bool loadPendingAlert(Alert& out) { return alertPeek(0, out); }
  virtual bool savePendingAlert(const Alert& a) { return alertPush(a); }
  virtual void clearPendingAlert() { alertDrop(1); }
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
  // The same, waiting until it finishes (up to the time limits: WIFI_CONNECT_TIMEOUT_S
  // per network for connect, NTP_SYNC_TIMEOUT_MS for sync). Only for the wake cycle in setup() and the blocking
  // runCommand(); never from loop(). The spin doesn't hog the CPU: the real
  // pollConnect()/pollSync() give it up for a tick while Pending.
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
  // After a sendReadings() that returned false: true when the server answered
  // and refused the readings themselves (HTTP 400, 409, 413, 422), false for a
  // network, timeout, auth or server-side failure. One bad reading refuses a
  // whole post (#7).
  virtual bool lastSendRefused() { return false; }
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
  // timerS is never 0: the wake cycle asks for at least 1 s.
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
