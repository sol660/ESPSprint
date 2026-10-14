#include <unity.h>

#include "fakes/Fakes.h"
#include "WakeCycle.h"

// Small windows keep the scripted sample lists short.
static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  return cfg;
}

static void scriptStill(FakeImu& imu, float ax, float ay, float az, int count) {
  for (int i = 0; i < count; ++i) imu.samples.push_back({ax, ay, az, 0, 0, 0});
}

static void storeZeroBaseline(FakeStore& store) {
  store.hasBaseline = true;
  store.baseline = {0.0f, 0.0f};
}

// Alternates az between 1 +/- jitter: per-window std dev = jitter (g).
static void scriptJitter(FakeImu& imu, float ax, float jitter, int count) {
  for (int i = 0; i < count; ++i) {
    float az = 1.0f + ((i % 2) ? jitter : -jitter);
    imu.samples.push_back({ax, 0, az, 0, 0, 0});
  }
}

void setUp() {}
void tearDown() {}

void test_level_still_board_gives_stable_zero_reading() {
  FakeHardware hw;
  RtcState state;
  storeZeroBaseline(hw.store);
  scriptStill(hw.imu, 0.0f, 0.0f, 1.0f, 50);

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.tookReading);
  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.rollDeg);
}

// Worked example: gravity for pitch 10 deg, roll -20 deg is
// (-sin10, cos10*sin(-20), cos10*cos20).
void test_tilt_delta_is_reading_minus_stored_baseline() {
  FakeHardware hw;
  RtcState state;
  hw.store.hasBaseline = true;
  hw.store.baseline = {3.0f, -5.0f};
  scriptStill(hw.imu, -0.173648f, -0.336824f, 0.925417f, 50);

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, r.reading.pitchRaw);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, r.reading.rollRaw);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, r.reading.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -15.0f, r.reading.rollDeg);
}

void test_all_windows_noisy_gives_unstable_reading() {
  FakeHardware hw;
  RtcState state;
  storeZeroBaseline(hw.store);
  scriptJitter(hw.imu, 0.0f, 0.01f, 200);

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.tookReading);
  TEST_ASSERT_FALSE(r.reading.stable);
}

void test_noisy_windows_are_left_out_of_the_average() {
  FakeHardware hw;
  RtcState state;
  storeZeroBaseline(hw.store);
  // Noisy windows tilted hard (ax = 0.5 g) between still, level ones.
  for (int w = 0; w < 5; ++w) {
    scriptJitter(hw.imu, 0.5f, 0.01f, 10);
    scriptStill(hw.imu, 0.0f, 0.0f, 1.0f, 10);
  }

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.pitchDeg);
}

void test_gyro_motion_rejects_windows_even_when_accel_is_still() {
  FakeHardware hw;
  RtcState state;
  storeZeroBaseline(hw.store);
  // Vibration: gyro x alternates +/-5 deg/s. (A constant rate is just the
  // MPU6050 zero-rate offset, so the gate looks at spread, not level.)
  for (int i = 0; i < 200; ++i) hw.imu.samples.push_back({0, 0, 1.0f, (i % 2) ? 5.0f : -5.0f, 0, 0});

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.reading.stable);
}

void test_reading_gives_up_at_the_time_limit_when_never_still() {
  FakeHardware hw;
  RtcState state;
  storeZeroBaseline(hw.store);
  scriptJitter(hw.imu, 0.0f, 0.01f, 1000);
  hw.clock.msPerUptimeRead = 50;
  CycleConfig cfg = testConfig();
  cfg.readingTimeLimitMs = 1000;

  WakeResult r = runWakeCycle(WakeCause::Timer, state, cfg, hw.ports());

  TEST_ASSERT_FALSE(r.reading.stable);
  TEST_ASSERT_TRUE_MESSAGE(hw.imu.samples.size() > 0, "kept reading past the time limit");
}

void test_reading_reports_its_noise_in_g() {
  FakeHardware hw;
  RtcState state;
  storeZeroBaseline(hw.store);
  scriptJitter(hw.imu, 0.0f, 0.001f, 50);  // std dev 0.001 g: under the gate

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, r.reading.noise);
}

void test_first_boot_without_baseline_captures_and_saves_one() {
  FakeHardware hw;
  RtcState state;
  scriptStill(hw.imu, -0.173648f, -0.336824f, 0.925417f, 300);  // pitch 10, roll -20
  CycleConfig cfg = testConfig();
  cfg.baselineWindows = 10;

  WakeResult r = runWakeCycle(WakeCause::PowerOn, state, cfg, hw.ports());

  TEST_ASSERT_EQUAL(1, hw.store.baselineSaves);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, hw.store.baseline.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, hw.store.baseline.rollDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.rollDeg);
}

void test_first_boot_while_shaking_saves_no_baseline() {
  FakeHardware hw;
  RtcState state;
  scriptJitter(hw.imu, 0.0f, 0.01f, 300);
  CycleConfig cfg = testConfig();
  cfg.baselineWindows = 10;

  runWakeCycle(WakeCause::PowerOn, state, cfg, hw.ports());

  TEST_ASSERT_EQUAL(0, hw.store.baselineSaves);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_level_still_board_gives_stable_zero_reading);
  RUN_TEST(test_tilt_delta_is_reading_minus_stored_baseline);
  RUN_TEST(test_all_windows_noisy_gives_unstable_reading);
  RUN_TEST(test_noisy_windows_are_left_out_of_the_average);
  RUN_TEST(test_gyro_motion_rejects_windows_even_when_accel_is_still);
  RUN_TEST(test_reading_gives_up_at_the_time_limit_when_never_still);
  RUN_TEST(test_reading_reports_its_noise_in_g);
  RUN_TEST(test_first_boot_without_baseline_captures_and_saves_one);
  RUN_TEST(test_first_boot_while_shaking_saves_no_baseline);
  return UNITY_END();
}
