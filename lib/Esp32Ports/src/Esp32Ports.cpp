#include "Esp32Ports.h"

#include <esp_sleep.h>
#include <time.h>

#include "config.h"

// Placeholders: each reports failure until its ticket implements it.

bool Esp32Imu::readSample(ImuSample&) { return false; }
bool Esp32Imu::readTemperatureC(float&) { return false; }
bool Esp32Imu::setMotionThreshold(float) { return false; }
bool Esp32Imu::resetBus() { return false; }

uint32_t Esp32Clock::nowUtc() { return static_cast<uint32_t>(time(nullptr)); }
bool Esp32Clock::isSynced() { return false; }
uint32_t Esp32Clock::uptimeMs() { return millis(); }
bool Esp32Clock::sync() { return false; }

bool Esp32Store::loadBaseline(Baseline&) { return false; }
bool Esp32Store::saveBaseline(const Baseline&) { return false; }
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
