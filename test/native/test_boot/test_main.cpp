#include <unity.h>

#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

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

  // Only cold boots count: timer wakes keep the count (user decision, #19).
  TEST_ASSERT_EQUAL_UINT32(1, b.bootCount);
  TEST_ASSERT_TRUE(b.cause == WakeCause::Timer);
  TEST_ASSERT_EQUAL_UINT32(1, state.bootCount);
}

void test_wake_cause_names_match_the_boot_line_vocabulary() {
  TEST_ASSERT_EQUAL_STRING("PowerOn", wakeCauseName(WakeCause::PowerOn));
  TEST_ASSERT_EQUAL_STRING("Timer", wakeCauseName(WakeCause::Timer));
  TEST_ASSERT_EQUAL_STRING("Motion", wakeCauseName(WakeCause::Motion));
  TEST_ASSERT_EQUAL_STRING("Serial", wakeCauseName(WakeCause::Serial));
}

// With the Store: the boot count counts cold boots (power-on, reset, crash) and
// is kept in flash, so (device_id, seq, boot_count) never repeats after a power
// loss. seq lives in RTC memory and restarts at each cold boot.

void test_cold_boot_counts_on_from_the_count_in_flash() {
  FakeStore store;
  store.hasBootCount = true;
  store.bootCount = 41;
  RtcState state;  // power lost: RTC memory zeroed

  BootInfo b = startBoot(WakeCause::PowerOn, state, store);

  TEST_ASSERT_EQUAL_UINT32(42, b.bootCount);
  TEST_ASSERT_EQUAL_UINT32(42, store.bootCount);
}

void test_first_cold_boot_with_nothing_in_flash_is_boot_one() {
  FakeStore store;
  RtcState state;

  BootInfo b = startBoot(WakeCause::PowerOn, state, store);

  TEST_ASSERT_EQUAL_UINT32(1, b.bootCount);
  TEST_ASSERT_TRUE(store.hasBootCount);
  TEST_ASSERT_EQUAL_UINT32(1, store.bootCount);
}

void test_timer_wake_keeps_the_boot_count_and_does_not_write_flash() {
  FakeStore store;
  RtcState state;
  startBoot(WakeCause::PowerOn, state, store);
  const int saves = store.bootCountSaves;

  BootInfo b = startBoot(WakeCause::Timer, state, store);

  TEST_ASSERT_EQUAL_UINT32(1, b.bootCount);
  TEST_ASSERT_EQUAL_INT(saves, store.bootCountSaves);
}

void test_motion_wake_keeps_the_boot_count() {
  FakeStore store;
  RtcState state;
  startBoot(WakeCause::PowerOn, state, store);

  BootInfo b = startBoot(WakeCause::Motion, state, store);

  TEST_ASSERT_EQUAL_UINT32(1, b.bootCount);
  TEST_ASSERT_EQUAL_UINT32(1, store.bootCount);
}

void test_reset_counts_on_from_flash_not_from_rtc_memory() {
  // After EN or a crash, RTC memory may still hold an old count: flash decides.
  FakeStore store;
  store.hasBootCount = true;
  store.bootCount = 7;
  RtcState state;
  state.bootCount = 3;

  BootInfo b = startBoot(WakeCause::PowerOn, state, store);

  TEST_ASSERT_EQUAL_UINT32(8, b.bootCount);
}

void test_seq_restarts_on_a_cold_boot_and_carries_across_timer_wakes() {
  FakeStore store;
  RtcState state;
  state.seq = 9;  // left over from before a reset

  BootInfo cold = startBoot(WakeCause::PowerOn, state, store);
  TEST_ASSERT_EQUAL_UINT32(0, cold.seq);

  state.seq = 4;  // four readings taken since
  BootInfo timer = startBoot(WakeCause::Timer, state, store);
  TEST_ASSERT_EQUAL_UINT32(4, timer.seq);
}

void test_boot_line_reports_fw_boot_cause_and_seq() {
  char line[80];

  formatBootLine(BootInfo{"0.1.0", 42, WakeCause::Timer, 3}, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING("@boot fw=0.1.0 boot=42 cause=Timer seq=3", line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_after_power_on_is_boot_one);
  RUN_TEST(test_boot_count_carries_across_timer_wakes);
  RUN_TEST(test_wake_cause_names_match_the_boot_line_vocabulary);
  RUN_TEST(test_cold_boot_counts_on_from_the_count_in_flash);
  RUN_TEST(test_first_cold_boot_with_nothing_in_flash_is_boot_one);
  RUN_TEST(test_timer_wake_keeps_the_boot_count_and_does_not_write_flash);
  RUN_TEST(test_motion_wake_keeps_the_boot_count);
  RUN_TEST(test_reset_counts_on_from_flash_not_from_rtc_memory);
  RUN_TEST(test_seq_restarts_on_a_cold_boot_and_carries_across_timer_wakes);
  RUN_TEST(test_boot_line_reports_fw_boot_cause_and_seq);
  return UNITY_END();
}
