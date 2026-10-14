#include "Esp32Ports.h"

#include <Preferences.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <time.h>

#include "config.h"

// Placeholders report failure until their ticket implements them.

uint32_t Esp32Clock::nowUtc() { return static_cast<uint32_t>(time(nullptr)); }
bool Esp32Clock::isSynced() { return false; }
uint32_t Esp32Clock::uptimeMs() { return millis(); }
bool Esp32Clock::sync() { return false; }

bool Esp32Store::loadCalibration(TempCalibration&) { return false; }
bool Esp32Store::bufferPush(const Reading&) { return false; }
size_t Esp32Store::bufferCount() { return 0; }
bool Esp32Store::bufferPeek(size_t, Reading&) { return false; }
void Esp32Store::bufferDrop(size_t) {}
bool Esp32Store::loadPendingAlert(Alert&) { return false; }
bool Esp32Store::savePendingAlert(const Alert&) { return false; }
void Esp32Store::clearPendingAlert() {}

bool Esp32Uplink::sendReadings(const Reading*, size_t) { return false; }
bool Esp32Uplink::sendAlert(const Alert&) { return false; }

void Esp32CamTrigger::pulse() { log_w("CAM pulse requested (not wired yet)"); }

void Esp32Sleeper::sleep(uint32_t timerS, bool motionWake) {
  log_w("would deep-sleep %lu s (motion wake %s); deep sleep not implemented yet",
        static_cast<unsigned long>(timerS), motionWake ? "on" : "off");
}

WakeCause readWakeCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WakeCause::Timer;
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
      return WakeCause::Motion;
    default:
      return WakeCause::PowerOn;
  }
}

// ---- Esp32Imu: MPU6050 over I2C (registers from the MPU-6000/6050 register map) ----

namespace {

constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;  // 14 bytes: accel xyz, temp, gyro xyz
constexpr uint8_t REG_TEMP_OUT_H = 0x41;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr float ACCEL_LSB_PER_G = 16384.0f;  // +/-2 g
constexpr float GYRO_LSB_PER_DPS = 131.0f;   // +/-250 deg/s

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(config::MPU_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(config::MPU_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(config::MPU_I2C_ADDR, static_cast<uint8_t>(len)) != len) return false;
  for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

int16_t be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }

}  // namespace

bool Esp32Imu::begin() {
  Wire.begin(config::PIN_I2C_SDA, config::PIN_I2C_SCL, config::I2C_CLOCK_HZ);
  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) {
    log_e("MPU6050 not answering at 0x%02x", config::MPU_I2C_ADDR);
    return false;
  }
  if (who != 0x68) log_w("unexpected WHO_AM_I 0x%02x (clone?), continuing", who);
  // With the DLPF on, the internal rate is 1 kHz: rate = 1000 / (1 + div).
  const uint8_t div = static_cast<uint8_t>(1000 / config::IMU_SAMPLE_RATE_HZ - 1);
  return writeReg(REG_PWR_MGMT_1, 0x01) &&    // wake, clock from gyro X PLL
         writeReg(REG_CONFIG, 0x06) &&        // DLPF lowest setting (5 Hz)
         writeReg(REG_SMPLRT_DIV, div) &&
         writeReg(REG_GYRO_CONFIG, 0x00) &&   // +/-250 deg/s
         writeReg(REG_ACCEL_CONFIG, 0x00);    // +/-2 g
}

bool Esp32Imu::readSample(ImuSample& out) {
  // Pace to the sensor's output rate so consecutive samples aren't duplicates.
  const uint32_t periodUs = 1000000UL / config::IMU_SAMPLE_RATE_HZ;
  while (micros() - lastSampleUs_ < periodUs) {
  }
  lastSampleUs_ = micros();

  uint8_t b[14];
  if (!readRegs(REG_ACCEL_XOUT_H, b, sizeof b)) return false;
  out.ax = be16(b + 0) / ACCEL_LSB_PER_G;
  out.ay = be16(b + 2) / ACCEL_LSB_PER_G;
  out.az = be16(b + 4) / ACCEL_LSB_PER_G;
  out.gx = be16(b + 8) / GYRO_LSB_PER_DPS;
  out.gy = be16(b + 10) / GYRO_LSB_PER_DPS;
  out.gz = be16(b + 12) / GYRO_LSB_PER_DPS;
  return true;
}

bool Esp32Imu::readTemperatureC(float& out) {
  uint8_t b[2];
  if (!readRegs(REG_TEMP_OUT_H, b, sizeof b)) return false;
  out = be16(b) / 340.0f + 36.53f;
  return true;
}

bool Esp32Imu::setMotionThreshold(float) { return false; }  // wake-on-motion: #5

bool Esp32Imu::resetBus() {  // full bus recovery: #7
  Wire.end();
  return begin();
}

// ---- Esp32Store: flash via Preferences (NVS) ----

namespace {
constexpr const char* KEY_BASELINE = "baseline";
}

bool Esp32Store::loadBaseline(Baseline& out) {
  Preferences prefs;
  if (!prefs.begin(ns_, true)) return false;  // read-only; fails if never written
  const bool ok = prefs.getBytesLength(KEY_BASELINE) == sizeof(Baseline) &&
                  prefs.getBytes(KEY_BASELINE, &out, sizeof(Baseline)) == sizeof(Baseline);
  prefs.end();
  return ok;
}

bool Esp32Store::saveBaseline(const Baseline& b) {
  Preferences prefs;
  if (!prefs.begin(ns_, false)) return false;
  const bool ok = prefs.putBytes(KEY_BASELINE, &b, sizeof b) == sizeof b;
  prefs.end();
  if (!ok) log_e("baseline save to flash failed");
  return ok;
}

