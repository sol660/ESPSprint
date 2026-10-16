#include <unity.h>

#include "WakeCycle.h"
#include "fakes/Fakes.h"

// The sleep timer the wake cycle asks for always wakes the board again (code
// review of spec #14: the "0 s -> 1 s" rule belongs in the core, not the port).

void setUp() {}
void tearDown() {}

void test_a_zero_interval_still_sleeps_for_one_second() {
  FakeHardware hw;
  RtcState state;
  CycleConfig cfg;
  cfg.normalIntervalS = 0;

  WakeResult r = runWakeCycle(WakeCause::Timer, state, cfg, hw.ports());

  TEST_ASSERT_EQUAL_UINT32(1, r.nextWakeS);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_zero_interval_still_sleeps_for_one_second);
  return UNITY_END();
}
