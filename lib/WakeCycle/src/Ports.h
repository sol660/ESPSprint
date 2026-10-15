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
};

struct Alert {
  Level level;
  float tiltDeg;
  uint32_t ts;
};

class Imu {
 public:
  virtual ~Imu() = default;
  virtual bool readSample(ImuSample& out) = 0;
  virtual bool readTemperatureC(float& out) = 0;
  virtual bool setMotionThreshold(float g) = 0;
  virtual bool resetBus() = 0;
};

class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t nowUtc() = 0;
  virtual bool isSynced() = 0;
  virtual uint32_t uptimeMs() = 0;  // for in-wake time budgets
  virtual bool sync() = 0;
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

class Uplink {
 public:
  virtual ~Uplink() = default;
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
