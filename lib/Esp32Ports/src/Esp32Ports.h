#pragma once

// Real hardware behind the wake-cycle Ports. For now these are placeholders
// that log and report failure; later tickets fill each one in.

#include <Arduino.h>

#include "Ports.h"
#include "WakeCycle.h"

class Esp32Imu : public Imu {
 public:
  // Wakes the MPU6050 and sets +/-2 g, lowest DLPF, IMU_SAMPLE_RATE_HZ. False if it doesn't answer.
  bool begin();
  bool readSample(ImuSample& out) override;
  bool readTemperatureC(float& out) override;
  bool setMotionThreshold(float g) override;
  bool resetBus() override;

 private:
  uint32_t lastSampleUs_ = 0;
};

class Esp32Clock : public Clock {
 public:
  uint32_t nowUtc() override;
  bool isSynced() override;
  uint32_t uptimeMs() override;
  bool sync() override;
};

class Esp32Store : public Store {
 public:
  explicit Esp32Store(const char* ns = "espsprint") : ns_(ns) {}
  bool loadBaseline(Baseline& out) override;
  bool saveBaseline(const Baseline& b) override;
  bool loadCalibration(TempCalibration& out) override;
  bool bufferPush(const Reading& r) override;
  size_t bufferCount() override;
  bool bufferPeek(size_t index, Reading& out) override;
  void bufferDrop(size_t count) override;
  bool loadPendingAlert(Alert& out) override;
  bool savePendingAlert(const Alert& a) override;
  void clearPendingAlert() override;

 private:
  const char* ns_;  // Preferences (NVS) namespace
};

class Esp32Uplink : public Uplink {
 public:
  bool sendReadings(const Reading* readings, size_t count) override;
  bool sendAlert(const Alert& alert) override;
};

class Esp32CamTrigger : public CamTrigger {
 public:
  void pulse() override;
};

class Esp32Sleeper : public Sleeper {
 public:
  void sleep(uint32_t timerS, bool motionWake) override;
};

struct Esp32Ports {
  Esp32Imu imu;
  Esp32Clock clock;
  Esp32Store store;
  Esp32Uplink uplink;
  Esp32CamTrigger cam;
  Esp32Sleeper sleeper;
  Ports ports{imu, clock, store, uplink, cam, sleeper};
};

WakeCause readWakeCause();
