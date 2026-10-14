#include <unity.h>

#include "fakes/Fakes.h"
#include "WakeCycle.h"

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

// Gravity for pitch 10 deg, roll -20 deg: (-sin10, cos10*sin(-20), cos10*cos20).
static void scriptTilted10Minus20(FakeImu& imu, int count) {
  scriptStill(imu, -0.173648f, -0.336824f, 0.925417f, count);
}

void setUp() {}
void tearDown() {}

void test_baseline_command_recaptures_over_an_existing_baseline() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  hw.store.baseline = {0.0f, 0.0f};
  scriptTilted10Minus20(hw.imu, 200);

  CommandResult r = runCommand(Command::Baseline, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(1, hw.store.baselineSaves);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, hw.store.baseline.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, hw.store.baseline.rollDeg);
}

void test_read_command_measures_against_the_stored_baseline() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  hw.store.baseline = {3.0f, -5.0f};
  scriptTilted10Minus20(hw.imu, 200);

  CommandResult r = runCommand(Command::Read, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, r.reading.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -15.0f, r.reading.rollDeg);
  TEST_ASSERT_EQUAL(0, hw.store.baselineSaves);
}

void test_status_shows_baseline_and_the_last_wake_reading() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  hw.store.baseline = {3.0f, -5.0f};
  scriptTilted10Minus20(hw.imu, 200);
  runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  CommandResult r = runCommand(Command::Status, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.hasBaseline);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, r.baseline.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, r.baseline.rollDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, r.reading.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -15.0f, r.reading.rollDeg);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_baseline_command_recaptures_over_an_existing_baseline);
  RUN_TEST(test_read_command_measures_against_the_stored_baseline);
  RUN_TEST(test_status_shows_baseline_and_the_last_wake_reading);
  return UNITY_END();
}
