#include <unity.h>

#include "TaggedReply.h"
#include "WakeCycle.h"

// The command window (spec #14 "Command window and sleep"): after power-on or
// reset the board listens for installer commands for 60 s, each command keeps it
// awake until 5 min after the last one, and timer wakes go straight back to sleep.
// main.cpp feeds it millis() and handled commands; it says when to sleep.

void setUp() {}
void tearDown() {}

constexpr uint32_t SEC = 1000;
constexpr uint32_t OPENED = 7 * SEC;  // listening starts once the boot's wake cycle is done

void test_power_on_listens_for_60_s_then_sleeps() {
  CommandWindow w(WakeCause::PowerOn, OPENED, CycleConfig{});

  TEST_ASSERT_FALSE(w.sleepDue(OPENED));
  TEST_ASSERT_FALSE(w.sleepDue(OPENED + 60 * SEC - 1));
  TEST_ASSERT_TRUE(w.sleepDue(OPENED + 60 * SEC));
}

void test_a_command_keeps_the_board_awake_until_5_min_after_it() {
  CommandWindow w(WakeCause::PowerOn, OPENED, CycleConfig{});
  const uint32_t command = OPENED + 30 * SEC;
  w.commandHandled(command);

  TEST_ASSERT_FALSE(w.sleepDue(OPENED + 60 * SEC));  // past the initial window
  TEST_ASSERT_FALSE(w.sleepDue(command + 5 * 60 * SEC - 1));
  TEST_ASSERT_TRUE(w.sleepDue(command + 5 * 60 * SEC));
}

void test_each_command_restarts_the_5_min_from_the_last_one() {
  CommandWindow w(WakeCause::PowerOn, OPENED, CycleConfig{});
  w.commandHandled(OPENED + 10 * SEC);
  const uint32_t last = OPENED + 4 * 60 * SEC;
  w.commandHandled(last);

  TEST_ASSERT_FALSE(w.sleepDue(OPENED + 10 * SEC + 5 * 60 * SEC));
  TEST_ASSERT_FALSE(w.sleepDue(last + 5 * 60 * SEC - 1));
  TEST_ASSERT_TRUE(w.sleepDue(last + 5 * 60 * SEC));
}

void test_timer_wake_sleeps_straight_away_without_listening() {
  CommandWindow w(WakeCause::Timer, OPENED, CycleConfig{});

  TEST_ASSERT_TRUE(w.sleepDue(OPENED));
}

void test_window_lengths_come_from_the_cycle_config() {
  // The esp32dev_hil build uses short values; this is how they reach the window.
  CycleConfig cfg;
  cfg.commandWindowMs = 10 * SEC;
  cfg.sessionIdleMs = 20 * SEC;
  CommandWindow w(WakeCause::PowerOn, OPENED, cfg);

  TEST_ASSERT_FALSE(w.sleepDue(OPENED + 10 * SEC - 1));
  TEST_ASSERT_TRUE(w.sleepDue(OPENED + 10 * SEC));
  w.commandHandled(OPENED + 15 * SEC);
  TEST_ASSERT_FALSE(w.sleepDue(OPENED + 35 * SEC - 1));
  TEST_ASSERT_TRUE(w.sleepDue(OPENED + 35 * SEC));
}

void test_a_command_never_shortens_a_longer_initial_window() {
  CycleConfig cfg;
  cfg.commandWindowMs = 60 * SEC;
  cfg.sessionIdleMs = 10 * SEC;
  CommandWindow w(WakeCause::PowerOn, OPENED, cfg);
  w.commandHandled(OPENED + 5 * SEC);

  TEST_ASSERT_FALSE(w.sleepDue(OPENED + 60 * SEC - 1));
  TEST_ASSERT_TRUE(w.sleepDue(OPENED + 60 * SEC));
}

void test_window_survives_millis_wrapping_around() {
  const uint32_t opened = 0xFFFFFFFFu - 20 * SEC;  // millis() wraps after ~49.7 days
  CommandWindow w(WakeCause::PowerOn, opened, CycleConfig{});

  TEST_ASSERT_FALSE(w.sleepDue(opened + 30 * SEC));  // wrapped past zero
  TEST_ASSERT_TRUE(w.sleepDue(opened + 60 * SEC));
}

void test_sleep_line_gives_the_timer_seconds() {
  // Printed just before deep sleep; the board tests wait for it.
  char line[40];
  formatSleepLine(30, line, sizeof line);

  TEST_ASSERT_EQUAL_STRING("@sleep s=30", line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_power_on_listens_for_60_s_then_sleeps);
  RUN_TEST(test_a_command_keeps_the_board_awake_until_5_min_after_it);
  RUN_TEST(test_each_command_restarts_the_5_min_from_the_last_one);
  RUN_TEST(test_timer_wake_sleeps_straight_away_without_listening);
  RUN_TEST(test_window_lengths_come_from_the_cycle_config);
  RUN_TEST(test_a_command_never_shortens_a_longer_initial_window);
  RUN_TEST(test_window_survives_millis_wrapping_around);
  RUN_TEST(test_sleep_line_gives_the_timer_seconds);
  return UNITY_END();
}
