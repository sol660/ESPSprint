#include <unity.h>

#include <cmath>

#include "WakeCycle.h"
#include "fakes/Fakes.h"

// What installer commands do to the level rules (code review of spec #14):
// a new baseline starts the step rule again, and the `wifi` reading carries
// the device's level (spec #20: the level goes into each reading and upload).

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

// The board resting at pitchDeg, for enough samples for one reading or baseline.
static void restAt(FakeHardware& hw, float pitchDeg, int windows) {
  const CycleConfig cfg = testConfig();
  const float p = pitchDeg / 57.29577951f;
  const ImuSample s{-std::sin(p), 0, std::cos(p), 0, 0, 0};
  for (int i = 0; i < cfg.windowSamples * windows; ++i) hw.imu.samples.push_back(s);
}

void setUp() {}
void tearDown() {}

void test_a_new_baseline_does_not_raise_a_step_alert_on_the_next_wake() {
  FakeHardware hw;
  RtcState state;
  const CycleConfig cfg = testConfig();
  hw.store.hasBaseline = true;  // zero baseline
  startBoot(WakeCause::PowerOn, state, hw.store);
  restAt(hw, 0.8f, cfg.windowsPerReading);
  TEST_ASSERT_EQUAL(Level::Ok, runWakeCycle(WakeCause::Timer, state, cfg, hw.ports()).level);

  // The installer re-levels the mounting and takes a new baseline there.
  restAt(hw, 0.8f, cfg.baselineWindows);
  TEST_ASSERT_TRUE(runCommand(Command::Baseline, state, cfg, hw.ports()).ok);

  // Delta now about 0: a 0.8 deg jump from the old delta, but not a movement.
  restAt(hw, 0.8f, cfg.windowsPerReading);
  WakeResult r = runWakeCycle(WakeCause::Timer, state, cfg, hw.ports());

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.tiltDeg);
  TEST_ASSERT_EQUAL(Level::Ok, r.level);
  TEST_ASSERT_EQUAL(0, hw.uplink.alertsSent.size());
}

void test_the_wifi_reading_carries_the_current_level() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  startBoot(WakeCause::PowerOn, state, hw.store);
  state.levels.level = Level::Watch;
  restAt(hw, 1.2f, testConfig().windowsPerReading);

  CommandResult r = runCommand(Command::Wifi, state, testConfig(), hw.ports(), 42);

  TEST_ASSERT_EQUAL(Level::Watch, r.reading.level);
  TEST_ASSERT_EQUAL(Level::Watch, hw.uplink.batchesSent.at(0).at(0).level);
  TEST_ASSERT_EQUAL(Level::Watch, state.levels.level);  // a command reading doesn't move the level
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_baseline_does_not_raise_a_step_alert_on_the_next_wake);
  RUN_TEST(test_the_wifi_reading_carries_the_current_level);
  return UNITY_END();
}
