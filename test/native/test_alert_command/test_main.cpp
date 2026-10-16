#include <unity.h>

#include <cstring>

#include "AlertMessage.h"
#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

// The test-alert command and the ntfy message text (#20; spec #14 stories
// 50-53): `alert <n>` sends a ntfy message marked "TEST — not a real alert"
// with the number, without blocking loop(), and prints `@alert ok= tag=`.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

void setUp() {}
void tearDown() {}

void test_alert_command_joins_then_sends_a_test_alert_with_the_number() {
  FakeHardware hw;
  hw.uplink.pollsBeforeJoined = 2;
  hw.clock.synced = false;
  hw.clock.utc = 42;
  hw.clock.utcAfterSync = 1792800000;
  RtcState state;
  state.levels.level = Level::Watch;
  state.lastReading.tiltDeg = 1.25f;
  WifiCommand cmd;

  cmd.start(Command::Alert, 123456789, hw.ports());
  TEST_ASSERT_FALSE(cmd.poll(state, testConfig(), hw.ports()));
  TEST_ASSERT_FALSE(cmd.poll(state, testConfig(), hw.ports()));
  TEST_ASSERT_TRUE(cmd.busy());
  TEST_ASSERT_EQUAL(0, hw.uplink.alertAttempts);  // nothing sent while joining

  while (!cmd.poll(state, testConfig(), hw.ports())) {
  }

  const CommandResult& r = cmd.result();
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.alertTried);
  TEST_ASSERT_TRUE(r.alertSent);
  TEST_ASSERT_EQUAL(1, hw.uplink.alertsSent.size());
  const Alert& sent = hw.uplink.alertsSent[0];
  TEST_ASSERT_EQUAL_UINT32(123456789, sent.testTag);
  TEST_ASSERT_EQUAL(Level::Watch, sent.level);  // the level the device is at
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, sent.tiltDeg);
  TEST_ASSERT_EQUAL_UINT32(1792800000u, sent.ts);  // time synced first
  TEST_ASSERT_EQUAL_UINT32(123456789, r.alert.testTag);
  TEST_ASSERT_FALSE(hw.uplink.connected);  // radio off again
  TEST_ASSERT_FALSE(r.uploadTried);  // no reading, no upload
  TEST_ASSERT_EQUAL(0, hw.uplink.batchesSent.size());
}

void test_alert_command_offline_reports_the_failure_and_is_not_kept_for_retry() {
  FakeHardware hw;
  hw.uplink.up = false;
  RtcState state;

  CommandResult r = runCommand(Command::Alert, state, testConfig(), hw.ports(), 5);

  TEST_ASSERT_TRUE(r.alertTried);
  TEST_ASSERT_FALSE(r.alertSent);
  TEST_ASSERT_EQUAL_UINT32(5, r.alert.testTag);
  TEST_ASSERT_FALSE(hw.store.hasPendingAlert);  // a test is never retried on later wakes
  TEST_ASSERT_EQUAL(1, hw.uplink.disconnects);
}

void test_a_test_alert_leaves_a_pending_real_alert_and_the_level_alone() {
  FakeHardware hw;
  hw.store.hasPendingAlert = true;
  hw.store.pendingAlert = Alert{Level::Alert, 2.5f, 1792800000};
  RtcState state;
  state.levels.level = Level::Alert;

  runCommand(Command::Alert, state, testConfig(), hw.ports(), 7);

  TEST_ASSERT_TRUE(hw.store.hasPendingAlert);
  TEST_ASSERT_EQUAL(Level::Alert, hw.store.pendingAlert.level);
  TEST_ASSERT_EQUAL(Level::Alert, state.levels.level);
}

void test_the_board_stays_awake_while_the_alert_command_runs() {
  FakeHardware hw;
  hw.uplink.pollsBeforeJoined = 1000;
  RtcState state;
  CycleConfig cfg = testConfig();
  CommandWindow window(WakeCause::PowerOn, 0, cfg);
  WifiCommand cmd;

  cmd.start(Command::Alert, 9, hw.ports());
  window.commandStarted();
  for (int i = 0; i < 10; ++i) cmd.poll(state, cfg, hw.ports());

  TEST_ASSERT_TRUE(cmd.busy());
  TEST_ASSERT_FALSE(window.sleepDue(cfg.commandWindowMs + 1));
}

void test_alert_line_reports_the_result_and_number() {
  char line[96];
  Alert test{Level::Ok, 0.123f, 1792800000, 123456789};
  formatAlertLine(test, true, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@alert ok=1 tag=123456789 level=OK tilt=0.123", line);

  Alert real{Level::Watch, 1.25f, 1792800000};
  formatAlertLine(real, false, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@alert ok=0 tag=none level=WATCH tilt=1.250", line);
}

void test_a_test_message_says_it_is_a_test_and_carries_the_number() {
  char body[256];
  Alert test{Level::Ok, 0.12f, 1792800000, 123456789};

  formatAlertMessage(test, "node-01", body, sizeof body);

  TEST_ASSERT_EQUAL(0, std::strncmp(body, "TEST \xE2\x80\x94 not a real alert\n", 25));
  TEST_ASSERT_NOT_NULL(std::strstr(body, "123456789"));
  TEST_ASSERT_NOT_NULL(std::strstr(body, "node-01"));
}

void test_a_real_message_gives_the_level_tilt_delta_and_time() {
  char body[256];
  Alert real{Level::Alert, 2.345f, 1792800000};  // 2026-10-24T00:00:00Z

  formatAlertMessage(real, "node-01", body, sizeof body);

  TEST_ASSERT_EQUAL_STRING(
      "Level ALERT at node-01\n"
      "Tilt delta 2.35 deg\n"
      "Time 2026-10-24T00:00:00Z",
      body);
  TEST_ASSERT_NULL(std::strstr(body, "TEST"));
}

void test_a_message_from_before_the_first_time_sync_says_the_time_is_unknown() {
  char body[256];
  Alert real{Level::Watch, 1.2f, 42};

  formatAlertMessage(real, "node-01", body, sizeof body);

  TEST_ASSERT_NOT_NULL(std::strstr(body, "Time unknown (42 s after power-on)"));
}

void test_title_and_priority_follow_the_level() {
  char title[64];
  formatAlertTitle(Alert{Level::Alert, 2.5f, 0}, "node-01", title, sizeof title);
  TEST_ASSERT_EQUAL_STRING("Landslide monitor node-01: ALERT", title);
  formatAlertTitle(Alert{Level::Watch, 2.5f, 0, 9}, "node-01", title, sizeof title);
  TEST_ASSERT_EQUAL_STRING("Landslide monitor node-01: TEST", title);

  TEST_ASSERT_EQUAL(5, alertPriority(Alert{Level::Alert, 0, 0}));
  TEST_ASSERT_EQUAL(4, alertPriority(Alert{Level::Watch, 0, 0}));
  TEST_ASSERT_EQUAL(3, alertPriority(Alert{Level::Alert, 0, 0, 9}));  // a test is never urgent
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_alert_command_joins_then_sends_a_test_alert_with_the_number);
  RUN_TEST(test_alert_command_offline_reports_the_failure_and_is_not_kept_for_retry);
  RUN_TEST(test_a_test_alert_leaves_a_pending_real_alert_and_the_level_alone);
  RUN_TEST(test_the_board_stays_awake_while_the_alert_command_runs);
  RUN_TEST(test_alert_line_reports_the_result_and_number);
  RUN_TEST(test_a_test_message_says_it_is_a_test_and_carries_the_number);
  RUN_TEST(test_a_real_message_gives_the_level_tilt_delta_and_time);
  RUN_TEST(test_a_message_from_before_the_first_time_sync_says_the_time_is_unknown);
  RUN_TEST(test_title_and_priority_follow_the_level);
  return UNITY_END();
}
