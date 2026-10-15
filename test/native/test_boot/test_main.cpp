#include <unity.h>

#include "WakeCycle.h"

// startBoot() runs first in every boot, before any slow work, and feeds the
// `@boot fw= boot= cause=` banner the board tests wait for.

void setUp() {}
void tearDown() {}

void test_first_boot_after_power_on_is_boot_one() {
  RtcState state;  // zeroed, as RTC memory is on power-on

  BootInfo b = startBoot(WakeCause::PowerOn, state);

  TEST_ASSERT_EQUAL_UINT32(1, b.bootCount);
  TEST_ASSERT_TRUE(b.cause == WakeCause::PowerOn);
  TEST_ASSERT_EQUAL_STRING("0.1.0", b.fwVersion);
}

void test_boot_count_carries_across_timer_wakes() {
  RtcState state;
  startBoot(WakeCause::PowerOn, state);
  startBoot(WakeCause::Timer, state);

  BootInfo b = startBoot(WakeCause::Timer, state);

  TEST_ASSERT_EQUAL_UINT32(3, b.bootCount);
  TEST_ASSERT_TRUE(b.cause == WakeCause::Timer);
  TEST_ASSERT_EQUAL_UINT32(3, state.bootCount);
}

void test_wake_cause_names_match_the_boot_line_vocabulary() {
  TEST_ASSERT_EQUAL_STRING("PowerOn", wakeCauseName(WakeCause::PowerOn));
  TEST_ASSERT_EQUAL_STRING("Timer", wakeCauseName(WakeCause::Timer));
  TEST_ASSERT_EQUAL_STRING("Motion", wakeCauseName(WakeCause::Motion));
  TEST_ASSERT_EQUAL_STRING("Serial", wakeCauseName(WakeCause::Serial));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_after_power_on_is_boot_one);
  RUN_TEST(test_boot_count_carries_across_timer_wakes);
  RUN_TEST(test_wake_cause_names_match_the_boot_line_vocabulary);
  return UNITY_END();
}
