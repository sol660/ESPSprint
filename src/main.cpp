#include <Arduino.h>

#include "Esp32Ports.h"
#include "WakeCycle.h"
#include "config.h"

// Survives deep sleep; zeroed on power-on.
RTC_DATA_ATTR RtcState rtcState;

void setup() {
  Serial.begin(config::SERIAL_BAUD);

  static Esp32Ports hw;
  WakeResult result = runWakeCycle(readWakeCause(), rtcState, CycleConfig{}, hw.ports);
  hw.ports.sleeper.sleep(result.nextWakeS, result.motionWake);
}

void loop() {}
