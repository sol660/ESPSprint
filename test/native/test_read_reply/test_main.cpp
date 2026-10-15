#include <unity.h>

#include "fakes/Fakes.h"
#include "TaggedReply.h"
#include "WakeCycle.h"

// What `read` reports beyond pitch and roll (#17): the board tests check these
// over serial in the tagged `@read` line.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

static void scriptStill(FakeImu& imu, float ax, float ay, float az, int count) {
  for (int i = 0; i < count; ++i) imu.samples.push_back({ax, ay, az, 0, 0, 0});
}

void setUp() {}
void tearDown() {}

void test_read_reports_the_total_acceleration_in_g() {
  FakeHardware hw;
  RtcState state;
  scriptStill(hw.imu, 0.0f, 0.6f * 0.98f, 0.8f * 0.98f, 200);  // 0.98 g, tilted

  CommandResult r = runCommand(Command::Read, state, testConfig(), hw.ports());

  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.98f, r.reading.accelG);
}

void test_read_reports_the_combined_tilt_delta() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  hw.store.baseline = {3.0f, -5.0f};
  // Gravity for pitch 10 deg, roll -20 deg: delta pitch 7, roll -15.
  scriptStill(hw.imu, -0.173648f, -0.336824f, 0.925417f, 200);

  CommandResult r = runCommand(Command::Read, state, testConfig(), hw.ports());

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.553f, r.reading.tiltDeg);  // sqrt(7^2 + 15^2)
}

void test_read_reports_the_imu_temperature() {
  FakeHardware hw;
  RtcState state;
  hw.imu.temperatureC = 23.5f;
  scriptStill(hw.imu, 0.0f, 0.0f, 1.0f, 200);

  CommandResult r = runCommand(Command::Read, state, testConfig(), hw.ports());

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.5f, r.reading.tempC);
}

static Reading exampleReading() {
  Reading r{};
  r.pitchDeg = 7.0f;
  r.rollDeg = -15.0f;
  r.tiltDeg = 16.5529f;
  r.stable = true;
  r.noise = 0.00123f;
  r.tempC = 23.46f;
  r.accelG = 0.99871f;
  return r;
}

void test_read_reply_is_one_tagged_line() {
  CommandResult r;
  r.ok = true;
  r.reading = exampleReading();
  char line[160];

  formatReadReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING(
      "@read ok=1 pitch=7.000 roll=-15.000 tilt=16.553 stable=1 noise=0.00123 temp=23.5 g=0.9987",
      line);
}

void test_status_reply_shows_the_baseline_and_the_last_reading() {
  CommandResult r;
  r.ok = true;
  r.hasBaseline = true;
  r.baseline = {3.0f, -5.25f};
  r.reading = exampleReading();
  char line[200];

  formatStatusReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING(
      "@status ok=1 baseline=1 bpitch=3.000 broll=-5.250 pitch=7.000 roll=-15.000 tilt=16.553 "
      "stable=1 noise=0.00123 temp=23.5 g=0.9987",
      line);
}

void test_status_reply_without_a_baseline_says_so() {
  CommandResult r;
  r.ok = false;
  char line[200];

  formatStatusReply(r, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING(
      "@status ok=0 baseline=0 pitch=0.000 roll=0.000 tilt=0.000 stable=0 noise=0.00000 temp=0.0 "
      "g=0.0000",
      line);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_read_reply_is_one_tagged_line);
  RUN_TEST(test_status_reply_shows_the_baseline_and_the_last_reading);
  RUN_TEST(test_status_reply_without_a_baseline_says_so);
  RUN_TEST(test_read_reports_the_total_acceleration_in_g);
  RUN_TEST(test_read_reports_the_combined_tilt_delta);
  RUN_TEST(test_read_reports_the_imu_temperature);
  return UNITY_END();
}
