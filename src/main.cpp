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
// The `wifi` and `alert` commands run across loop() calls, so loop() never
// waits on WiFi. wifiCommand.command() says which one is running.
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

// `@alert ok= tag= level= tilt=`. ok=0: not delivered.
static void printAlert(const Alert& a, bool ok) {
  char line[96];
  formatAlertLine(a, ok, line, sizeof line);
  Serial.println(line);
}

// A random 9-digit number for `wifi` and `alert` without one.
static uint32_t randomTag() { return 100000000UL + esp_random() % 900000000UL; }

// Matches `<name>` or `<name> <n>` and gives the tag: <n> (board tests pass their
// own random number), or a random 9-digit one when there is none.
static bool parseTaggedCommand(const char* line, const char* name, uint32_t& testTag) {
  const size_t n = strlen(name);
  if (strncmp(line, name, n) != 0 || (line[n] != '\0' && line[n] != ' ')) return false;
  testTag = line[n] == ' ' ? strtoul(line + n + 1, nullptr, 10) : 0;
  if (!isTestTag(testTag)) testTag = randomTag();
  return true;
}

static void printCommandResult(Command cmd, const CommandResult& r) {
  if (cmd == Command::Alert) {
    Serial.printf("test alert %lu %s\n", static_cast<unsigned long>(r.alert.testTag),
                  r.alertSent ? "sent to ntfy" : "NOT sent (WiFi or ntfy failed)");
    printAlert(r.alert, r.alertSent);
    return;
  }
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

// Handles one typed line. `wifi` and `alert` only start here; loop() finishes them.
static void handleCommand(const char* line) {
  // `ping <n>` echoes <n> as `@pong <n>`: board tests use it to prove a live reply.
  if (strncmp(line, "ping ", 5) == 0 && line[5] != '\0') {
    Serial.printf("@pong %s\n", line + 5);
    window.commandHandled(millis());
    return;
  }

  Command cmd;
  uint32_t testTag = 0;
  if (parseTaggedCommand(line, "wifi", testTag)) {
    // `wifi [n]` tags the test row (Supabase test_tag) with the number.
    cmd = Command::Wifi;
    Serial.println("wifi: connecting, then reading and uploading (up to ~90 s)...");
  } else if (parseTaggedCommand(line, "alert", testTag)) {
    // `alert [n]` sends a ntfy test message with the number.
    cmd = Command::Alert;
    Serial.printf("alert: connecting, then sending test alert %lu to ntfy (up to ~80 s)...\n",
                  static_cast<unsigned long>(testTag));
  } else if (strcmp(line, "read") == 0) {
    cmd = Command::Read;
  } else if (strcmp(line, "baseline") == 0) {
    cmd = Command::Baseline;
    Serial.println("capturing baseline: keep the unit still...");
  } else if (strcmp(line, "status") == 0) {
    cmd = Command::Status;
  } else {
    Serial.println("commands: read, baseline, status, wifi [tag], alert [n], ping <n>");
    window.commandHandled(millis());
    return;
  }

  if (cmd == Command::Wifi || cmd == Command::Alert) {
    wifiCommand.start(cmd, testTag, hw.ports);  // loop() polls it until it is done
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
  if (wake.alertsDropped > 0) {
    log_w("alert queue full: %u oldest undelivered alert(s) dropped",
          static_cast<unsigned>(wake.alertsDropped));
  }
  if (wake.alertTried) printAlert(wake.alert, wake.alertSent);
  if (wake.uploadTried) printUpload(wake.reading, wake.uploaded);

  // Listening starts now, after the wake cycle, so the installer gets the whole
  // window. Commands typed during the cycle wait in the UART buffer.
  window = CommandWindow(boot.cause, millis(), CycleConfig{});
}

void loop() {
  if (wifiCommand.busy()) {
    // Polled, never waited on. Typed input waits in the UART buffer meanwhile.
    if (wifiCommand.poll(rtcState, CycleConfig{}, hw.ports)) {
      printCommandResult(wifiCommand.command(), wifiCommand.result());
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
