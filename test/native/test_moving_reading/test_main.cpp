#include <unity.h>

#include <cmath>
#include <cstring>

#include "ReadingJson.h"
#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

// A board that moves for the whole reading (user decision 2026-10-18: "moving
// readings can not be trusted"). No window is still, so no angle is measured:
// like a sensor error, the reading has no angles, instead of 0 minus the
// baseline looking like a tilt of the baseline's size.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.readingTimeLimitMs = 1000;
  cfg.bufferCapacity = 100;
  return cfg;
}

// Alternates az between 1 +/- 0.05 g, far noisier than the still gate, with
// enough samples that the reading's time limit ends it (not the sensor).
static void scriptShaking(FakeImu& imu) {
  for (int i = 0; i < 5000; ++i) imu.samples.push_back({0, 0, (i % 2) ? 1.05f : 0.95f, 0, 0, 0});
}

struct Site {
  FakeHardware hw;
  RtcState state;

  Site() {
    hw.store.hasBaseline = true;
    hw.store.baseline = {2.0f, -1.5f};
    hw.clock.msPerUptimeRead = 5;
    scriptShaking(hw.imu);
    startBoot(WakeCause::PowerOn, state, hw.store);
  }
  WakeResult wake() { return runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports()); }
};

void setUp() {}
void tearDown() {}

void test_a_board_moving_for_the_whole_reading_has_no_angles() {
  Site site;

  WakeResult r = site.wake();

  TEST_ASSERT_TRUE(r.tookReading);
  TEST_ASSERT_FALSE(r.reading.stable);
  TEST_ASSERT_EQUAL(ReadingEvent::None, r.reading.event);  // the sensor works
  TEST_ASSERT_TRUE(anglesMissing(r.reading));
}

void test_a_moving_row_has_null_angles_not_the_baseline() {
  Site site;
  WakeResult r = site.wake();
  TEST_ASSERT_TRUE(r.uploaded);

  char json[READING_JSON_BUF_SIZE];
  const UploadHealth h{"node-01", "0.1.0", nullptr, false, 0};
  TEST_ASSERT_NOT_EQUAL(
      0, encodeReadingJson(site.hw.uplink.batchesSent.at(0).at(0), h, json, sizeof json));
  TEST_ASSERT_NOT_NULL(std::strstr(json,
                                   "\"pitch_d\":null,\"roll_d\":null,\"tilt_d\":null,"
                                   "\"pitch_raw\":null,\"roll_raw\":null,"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"stable\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"event\":\"none\""));
}

void test_read_on_a_moving_board_replies_ok_0_with_no_angles() {
  Site site;

  CommandResult r = runCommand(Command::Read, site.state, testConfig(), site.hw.ports());

  TEST_ASSERT_FALSE(r.ok);
  char line[160];
  formatReadReply(r, line, sizeof line);
  const char* want = "@read ok=0 pitch=nan roll=nan tilt=nan stable=0 ";
  TEST_ASSERT_EQUAL_STRING_LEN(want, line, std::strlen(want));
}

void test_a_moving_reading_does_not_change_the_level() {
  Site site;

  WakeResult r = site.wake();

  TEST_ASSERT_EQUAL(Level::Ok, r.level);
  TEST_ASSERT_EQUAL(0, site.hw.store.alertCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_board_moving_for_the_whole_reading_has_no_angles);
  RUN_TEST(test_a_moving_row_has_null_angles_not_the_baseline);
  RUN_TEST(test_read_on_a_moving_board_replies_ok_0_with_no_angles);
  RUN_TEST(test_a_moving_reading_does_not_change_the_level);
  return UNITY_END();
}
