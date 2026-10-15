#include <Arduino.h>

#include "Esp32Ports.h"
#include "TaggedReply.h"
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
  // `ping <n>` echoes <n> as `@pong <n>`: board tests use it to prove a live reply.
  if (strncmp(line, "ping ", 5) == 0 && line[5] != '\0') {
    Serial.printf("@pong %s\n", line + 5);
    return;
  }

  Command cmd;
  if (strcmp(line, "read") == 0) {
    cmd = Command::Read;
  } else if (strcmp(line, "baseline") == 0) {
    cmd = Command::Baseline;
    Serial.println("capturing baseline: keep the unit still...");
  } else if (strcmp(line, "status") == 0) {
    cmd = Command::Status;
  } else {
    Serial.println("commands: read, baseline, status, ping <n>");
    return;
  }

  const CommandResult r = runCommand(cmd, rtcState, CycleConfig{}, hw.ports);
  if (!r.ok) {
    Serial.println("failed (unit moving, or flash write error)");
  } else {
    if (r.hasBaseline) {
      Serial.printf("baseline pitch %+.3f roll %+.3f deg\n", r.baseline.pitchDeg, r.baseline.rollDeg);
    } else {
      Serial.println("no baseline stored");
    }
    if (cmd != Command::Baseline) printReading(r.reading);
  }

  // Tagged reply for board tests (ADR 0001): `read` -> @read, `baseline`/`status` -> @status.
  char reply[200];
  if (cmd == Command::Read) {
    formatReadReply(r, reply, sizeof reply);
  } else {
    formatStatusReply(r, reply, sizeof reply);
  }
  Serial.println(reply);
}

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  // Boot banner first, before any slow work: board tests wait for it after a reset.
  const BootInfo boot = startBoot(readWakeCause(), rtcState);
  Serial.printf("\n@boot fw=%s boot=%lu cause=%s\n", boot.fwVersion,
                static_cast<unsigned long>(boot.bootCount), wakeCauseName(boot.cause));

  if (!hw.imu.begin()) log_e("MPU6050 init failed: check SDA21/SCL22 wiring");

  WakeResult result = runWakeCycle(boot.cause, rtcState, CycleConfig{}, hw.ports);
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
      while (len > 0 && line[len - 1] == ' ') --len;  // trailing spaces
      line[len] = '\0';
      if (len > 0) handleCommand(line);
      len = 0;
    } else if (len < sizeof line - 1 && !(c == ' ' && len == 0)) {  // skip leading spaces
      line[len++] = c;
    }
  }
}
