#include <Arduino.h>

#include "Esp32Ports.h"
#include "WakeCycle.h"
#include "config.h"

// Survives deep sleep; zeroed on power-on.
RTC_DATA_ATTR RtcState rtcState;

static Esp32Ports hw;

static void printReading(const Reading& r) {
  Serial.printf("tilt delta pitch %+.3f roll %+.3f deg | raw pitch %+.3f roll %+.3f | noise %.4f g | %s\n",
                r.pitchDeg, r.rollDeg, r.pitchRaw, r.rollRaw, r.noise,
                r.stable ? "stable" : "UNSTABLE");
}

static void handleCommand(const char* line) {
  Command cmd;
  if (strcmp(line, "read") == 0) {
    cmd = Command::Read;
  } else if (strcmp(line, "baseline") == 0) {
    cmd = Command::Baseline;
    Serial.println("capturing baseline: keep the unit still...");
  } else if (strcmp(line, "status") == 0) {
    cmd = Command::Status;
  } else {
    Serial.println("commands: read, baseline, status");
    return;
  }

  const CommandResult r = runCommand(cmd, rtcState, CycleConfig{}, hw.ports);
  if (!r.ok) {
    Serial.println("failed (unit moving, or flash write error)");
    return;
  }
  if (r.hasBaseline) {
    Serial.printf("baseline pitch %+.3f roll %+.3f deg\n", r.baseline.pitchDeg, r.baseline.rollDeg);
  } else {
    Serial.println("no baseline stored");
  }
  if (cmd != Command::Baseline) printReading(r.reading);
}

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  if (!hw.imu.begin()) log_e("MPU6050 init failed: check SDA21/SCL22 wiring");

  WakeResult result = runWakeCycle(readWakeCause(), rtcState, CycleConfig{}, hw.ports);
  printReading(result.reading);
  hw.ports.sleeper.sleep(result.nextWakeS, result.motionWake);
}

void loop() {
  // Serial commands, read without blocking. Fixed buffer: no heap use in loop().
  static char line[33];
  static size_t len = 0;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      line[len] = '\0';
      if (len > 0) handleCommand(line);
      len = 0;
    } else if (len < sizeof line - 1 && c != ' ') {
      line[len++] = c;
    }
  }
}
