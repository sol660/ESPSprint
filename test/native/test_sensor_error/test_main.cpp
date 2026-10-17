#include <unity.h>

#include "WakeCycle.h"
#include "fakes/Fakes.h"

// Sensor failure (#7, spec #1 story 71): if the MPU6050 stops answering, the
// wake resets the I2C bus and tries again. If it still doesn't answer, the
// reading goes out with event=sensor_error (the uplink adds the usual health
// fields), so a sensor fault shows on the dashboard instead of going silent.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  cfg.bufferCapacity = 100;
  return cfg;
}

// An IMU that stops answering after `answerFor` samples until the bus is reset.
// After a reset it answers again only when `recoversOnReset`.
class DroppingImu : public FakeImu {
 public:
  int answerFor = 0;
  bool recoversOnReset = false;
  bool readSample(ImuSample& out) override {
    if (answerFor <= 0) return false;
    --answerFor;
    out = {0, 0, 1, 0, 0, 0};
    return true;
  }
  bool readTemperatureC(float& out) override {
    out = 21.0f;
    return answerFor > 0;
  }
  bool resetBus() override {
    ++busResets;
    if (recoversOnReset) answerFor = 1000;
    return recoversOnReset;
  }
};

struct Site {
  FakeHardware hw;
  DroppingImu imu;
  Ports ports{imu, hw.clock, hw.store, hw.uplink, hw.cam, hw.sleeper};
  RtcState state;

  Site() {
    hw.store.hasBaseline = true;
    startBoot(WakeCause::PowerOn, state, hw.store);
  }
  WakeResult wake() { return runWakeCycle(WakeCause::Timer, state, testConfig(), ports); }
};

void setUp() {}
void tearDown() {}

void test_a_sensor_that_does_not_answer_gives_a_sensor_error_reading() {
  Site site;  // answers nothing, a reset doesn't help

  WakeResult r = site.wake();

  TEST_ASSERT_TRUE(r.tookReading);
  TEST_ASSERT_EQUAL(1, site.imu.busResets);
  TEST_ASSERT_EQUAL(ReadingEvent::SensorError, r.reading.event);
  TEST_ASSERT_FALSE(r.reading.stable);
  TEST_ASSERT_EQUAL_UINT32(1, r.reading.seq);
  // It still goes up, so the fault shows on the dashboard.
  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(ReadingEvent::SensorError, site.hw.uplink.batchesSent.at(0).at(0).event);
}

void test_a_sensor_error_does_not_change_the_level() {
  Site site;
  site.state.levels.level = Level::Watch;

  WakeResult r = site.wake();

  TEST_ASSERT_EQUAL(Level::Watch, r.level);
  TEST_ASSERT_FALSE(r.alertTried);
}

void test_a_sensor_error_reading_is_buffered_like_any_other_when_offline() {
  Site site;
  site.hw.uplink.up = false;

  site.wake();

  TEST_ASSERT_EQUAL(1, site.hw.store.buffer.size());
  TEST_ASSERT_EQUAL(ReadingEvent::SensorError, site.hw.store.buffer[0].event);
}

void test_a_sensor_that_answers_again_after_a_bus_reset_gives_a_normal_reading() {
  Site site;
  site.imu.answerFor = 15;  // stops part way through the second window
  site.imu.recoversOnReset = true;

  WakeResult r = site.wake();

  TEST_ASSERT_EQUAL(1, site.imu.busResets);
  TEST_ASSERT_EQUAL(ReadingEvent::None, r.reading.event);
  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.reading.tiltDeg);
}

void test_a_sensor_that_fails_again_after_the_retry_gives_a_sensor_error() {
  Site site;
  site.imu.answerFor = 0;
  site.imu.recoversOnReset = true;  // the reset seems to work...
  WakeResult first = site.wake();
  TEST_ASSERT_EQUAL(ReadingEvent::None, first.reading.event);

  // ...but on the next wake it drops out again and the reset doesn't bring it back.
  site.imu.answerFor = 0;
  site.imu.recoversOnReset = false;
  WakeResult r = site.wake();

  TEST_ASSERT_EQUAL(ReadingEvent::SensorError, r.reading.event);
  TEST_ASSERT_EQUAL(2, site.imu.busResets);
}

void test_a_working_sensor_is_never_reset() {
  FakeHardware hw;
  hw.store.hasBaseline = true;
  for (int i = 0; i < 50; ++i) hw.imu.samples.push_back({0, 0, 1, 0, 0, 0});
  RtcState state;

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_EQUAL(0, hw.imu.busResets);
  TEST_ASSERT_EQUAL(ReadingEvent::None, r.reading.event);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_sensor_that_does_not_answer_gives_a_sensor_error_reading);
  RUN_TEST(test_a_sensor_error_does_not_change_the_level);
  RUN_TEST(test_a_sensor_error_reading_is_buffered_like_any_other_when_offline);
  RUN_TEST(test_a_sensor_that_answers_again_after_a_bus_reset_gives_a_normal_reading);
  RUN_TEST(test_a_sensor_that_fails_again_after_the_retry_gives_a_sensor_error);
  RUN_TEST(test_a_working_sensor_is_never_reset);
  return UNITY_END();
}
