#include <Arduino.h>

#include "Esp32Ports.h"
#include "TaggedReply.h"
#include "WakeCycle.h"
#include "config.h"

// Survives deep sleep; zeroed on power-on.
RTC_DATA_ATTR RtcState rtcState;

static Esp32Ports hw;
// This boot's wake-cycle result: the timer to sleep for once the window closes.
static WakeResult wake;
// Replaced in setup() with this boot's real cause and start time.
static CommandWindow window(WakeCause::Timer, 0, CycleConfig{});
// The `wifi` command runs across loop() calls, so loop() never waits on WiFi.
static WifiCommand wifiCommand;

static void printReading(const Reading& r) {
  Serial.printf("tilt delta pitch %+.3f roll %+.3f deg | raw pitch %+.3f roll %+.3f | noise %.4f g | %s\n",
                r.pitchDeg, r.rollDeg, r.pitchRaw, r.rollRaw, r.noise,
                r.stable ? "stable" : "UNSTABLE");
}

// `@upload ok= tag= seq= boot=`. ok=0: the reading was dropped (no buffer yet).
static void printUpload(const Reading& r, bool ok) {
  char line[80];
  formatUploadLine(r, ok, line, sizeof line);
  Serial.println(line);
}

static void printCommandResult(Command cmd, const CommandResult& r) {
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
  // `wifi` -> @read then @upload.
  char reply[200];
  if (cmd == Command::Read || cmd == Command::Wifi) {
    formatReadReply(r, reply, sizeof reply);
  } else {
    formatStatusReply(r, reply, sizeof reply);
  }
  Serial.println(reply);
  if (r.uploadTried) printUpload(r.reading, r.uploaded);
}

// Handles one typed line. `wifi` only starts here; loop() finishes it.
static void handleCommand(const char* line) {
  // `ping <n>` echoes <n> as `@pong <n>`: board tests use it to prove a live reply.
  if (strncmp(line, "ping ", 5) == 0 && line[5] != '\0') {
    Serial.printf("@pong %s\n", line + 5);
    window.commandHandled(millis());
    return;
  }

  Command cmd;
  uint32_t testTag = 0;
  if (strcmp(line, "wifi") == 0 || strncmp(line, "wifi ", 5) == 0) {
    // `wifi <n>` tags the test row with <n> (board tests pass their own random
    // number); plain `wifi` picks a random 9-digit tag.
    cmd = Command::Wifi;
    testTag = line[4] == ' ' ? strtoul(line + 5, nullptr, 10) : 0;
    if (testTag == 0) testTag = 100000000UL + esp_random() % 900000000UL;
    Serial.println("wifi: connecting, then reading and uploading (up to ~90 s)...");
  } else if (strcmp(line, "read") == 0) {
    cmd = Command::Read;
  } else if (strcmp(line, "baseline") == 0) {
    cmd = Command::Baseline;
    Serial.println("capturing baseline: keep the unit still...");
  } else if (strcmp(line, "status") == 0) {
    cmd = Command::Status;
  } else {
    Serial.println("commands: read, baseline, status, wifi [tag], ping <n>");
    window.commandHandled(millis());
    return;
  }

  if (cmd == Command::Wifi) {
    wifiCommand.start(testTag, hw.ports);  // loop() polls it until it is done
    window.commandStarted();
    return;
  }
  printCommandResult(cmd, runCommand(cmd, rtcState, CycleConfig{}, hw.ports));
  window.commandHandled(millis());  // after the command: a slow one doesn't eat the idle time
}

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  // Boot banner first, before any slow work: board tests wait for it after a reset.
  // Cold boots count on from the count in flash; timer wakes keep it.
  const BootInfo boot = startBoot(readWakeCause(), rtcState, hw.store);
  char bootLine[80];
  formatBootLine(boot, bootLine, sizeof bootLine);
  Serial.printf("\n%s\n", bootLine);

  if (!hw.imu.begin()) log_e("MPU6050 init failed: check SDA21/SCL22 wiring");

  wake = runWakeCycle(boot.cause, rtcState, CycleConfig{}, hw.ports);
  printReading(wake.reading);
  if (wake.uploadTried) printUpload(wake.reading, wake.uploaded);

  // Listening starts now, after the wake cycle, so the installer gets the whole
  // window. Commands typed during the cycle wait in the UART buffer.
  window = CommandWindow(boot.cause, millis(), CycleConfig{});
}

void loop() {
  if (wifiCommand.busy()) {
    // Polled, never waited on. Typed input waits in the UART buffer meanwhile.
    if (wifiCommand.poll(rtcState, CycleConfig{}, hw.ports)) {
      printCommandResult(Command::Wifi, wifiCommand.result());
      window.commandHandled(millis());
    }
    return;
  }

  if (window.sleepDue(millis())) {
    char line[24];
    formatSleepLine(wake.nextWakeS, line, sizeof line);
    Serial.println(line);
    Serial.flush();  // let the line leave the UART before the chip powers down
    hw.ports.sleeper.sleep(wake.nextWakeS, wake.motionWake);
    return;  // not reached: deep sleep ends in a fresh boot
  }

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
      if (wifiCommand.busy()) return;  // the rest of the input waits until it is done
    } else if (len < sizeof line - 1 && !(c == ' ' && len == 0)) {  // skip leading spaces
      line[len++] = c;
    }
  }
}
