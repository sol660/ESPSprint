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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_timer_wake_with_default_state_sleeps_15_minutes);
  return UNITY_END();
}
