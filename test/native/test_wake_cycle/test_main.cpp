#include <unity.h>

#include "fakes/Fakes.h"
#include "WakeCycle.h"

void setUp() {}
void tearDown() {}

void test_timer_wake_with_default_state_sleeps_15_minutes() {
  FakeHardware hw;
  RtcState state;

  WakeResult result = runWakeCycle(WakeCause::Timer, state, CycleConfig{}, hw.ports());

  TEST_ASSERT_EQUAL_UINT32(15 * 60, result.nextWakeS);
}

void test_each_wake_numbers_its_reading_within_the_cold_boot() {
  FakeHardware hw;
  RtcState state;
  startBoot(WakeCause::PowerOn, state, hw.store);

  WakeResult first = runWakeCycle(WakeCause::PowerOn, state, CycleConfig{}, hw.ports());
  startBoot(WakeCause::Timer, state, hw.store);
  WakeResult second = runWakeCycle(WakeCause::Timer, state, CycleConfig{}, hw.ports());

  TEST_ASSERT_EQUAL_UINT32(1, first.reading.seq);
  TEST_ASSERT_EQUAL_UINT32(2, second.reading.seq);
  TEST_ASSERT_EQUAL_UINT32(1, first.reading.bootCount);
  TEST_ASSERT_EQUAL_UINT32(1, second.reading.bootCount);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_timer_wake_with_default_state_sleeps_15_minutes);
  RUN_TEST(test_each_wake_numbers_its_reading_within_the_cold_boot);
  return UNITY_END();
}
