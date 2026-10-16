#include <unity.h>

#include "WakeCycle.h"
#include "fakes/Fakes.h"

// The `wifi` command without blocking loop() (CLAUDE.md rule; user decision on
// #14): main.cpp starts it, then polls it once per loop() until it is done.
// Joining WiFi and syncing the time are polled, never waited on.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

static FakeHardware& stillBoard(FakeHardware& hw) {
  hw.store.hasBaseline = true;
  for (int i = 0; i < 400; ++i) hw.imu.samples.push_back({0, 0, 1, 0, 0, 0});
  return hw;
}

void setUp() {}
void tearDown() {}

void test_wifi_command_waits_for_the_network_without_reading_or_uploading() {
  FakeHardware hw;
  stillBoard(hw);
  hw.uplink.pollsBeforeJoined = 3;
  RtcState state;
  WifiCommand cmd;

  cmd.start(123456789, hw.ports());

  for (int i = 0; i < 3; ++i) TEST_ASSERT_FALSE(cmd.poll(state, testConfig(), hw.ports()));
  TEST_ASSERT_TRUE(cmd.busy());
  TEST_ASSERT_EQUAL(400, hw.imu.samples.size());  // no reading yet
  TEST_ASSERT_EQUAL(0, hw.uplink.batchesSent.size());
  TEST_ASSERT_EQUAL(0, state.seq);
}

void test_wifi_command_syncs_the_time_then_reads_and_uploads_with_the_tag() {
  FakeHardware hw;
  stillBoard(hw);
  hw.uplink.pollsBeforeJoined = 2;
  hw.clock.pollsBeforeSynced = 2;
  hw.clock.synced = false;
  hw.clock.utc = 42;
  hw.clock.utcAfterSync = 1792800000;
  RtcState state;
  startBoot(WakeCause::PowerOn, state, hw.store);
  WifiCommand cmd;
  cmd.start(123456789, hw.ports());

  int polls = 1;
  while (!cmd.poll(state, testConfig(), hw.ports())) {
    TEST_ASSERT_EQUAL(0, hw.uplink.batchesSent.size());
    TEST_ASSERT_TRUE_MESSAGE(++polls < 20, "the wifi command never finished");
  }

  TEST_ASSERT_EQUAL(6, polls);  // 2 joining + joined, 2 syncing + synced
  TEST_ASSERT_FALSE(cmd.busy());
  TEST_ASSERT_EQUAL(1, hw.clock.syncCalls);
  const CommandResult& r = cmd.result();
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(1, hw.uplink.batchesSent.size());
  const Reading& sent = hw.uplink.batchesSent[0][0];
  TEST_ASSERT_EQUAL_UINT32(123456789, sent.testTag);
  TEST_ASSERT_TRUE(sent.timeSynced);
  TEST_ASSERT_EQUAL_UINT32(1792800000u, sent.ts);
  TEST_ASSERT_EQUAL_UINT32(1, sent.seq);
  TEST_ASSERT_FALSE(hw.uplink.connected);  // radio off again
}

void test_wifi_command_offline_reads_and_reports_the_failed_upload() {
  FakeHardware hw;
  stillBoard(hw);
  hw.uplink.up = false;
  hw.uplink.pollsBeforeJoined = 4;  // every network times out
  RtcState state;
  WifiCommand cmd;
  cmd.start(5, hw.ports());

  int polls = 1;
  while (!cmd.poll(state, testConfig(), hw.ports())) ++polls;

  TEST_ASSERT_EQUAL(5, polls);
  TEST_ASSERT_EQUAL(0, hw.clock.syncCalls);  // no time sync without a network
  const CommandResult& r = cmd.result();
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_FALSE(r.uploaded);
  TEST_ASSERT_EQUAL(1, hw.uplink.disconnects);
}

void test_a_failed_time_sync_still_uploads_marked_unsynced() {
  FakeHardware hw;
  stillBoard(hw);
  hw.clock.synced = false;
  hw.clock.syncSucceeds = false;
  hw.clock.pollsBeforeSynced = 3;
  hw.clock.utc = 42;
  RtcState state;
  WifiCommand cmd;
  cmd.start(7, hw.ports());

  while (!cmd.poll(state, testConfig(), hw.ports())) {
  }

  TEST_ASSERT_TRUE(cmd.result().uploaded);
  TEST_ASSERT_FALSE(hw.uplink.batchesSent.at(0).at(0).timeSynced);
}

void test_the_board_stays_awake_while_a_command_runs() {
  CycleConfig cfg;
  CommandWindow w(WakeCause::PowerOn, 0, cfg);
  w.commandStarted();

  TEST_ASSERT_FALSE(w.sleepDue(cfg.commandWindowMs + cfg.sessionIdleMs + 90 * 1000));

  const uint32_t done = cfg.commandWindowMs + cfg.sessionIdleMs + 90 * 1000;
  w.commandHandled(done);
  TEST_ASSERT_FALSE(w.sleepDue(done + cfg.sessionIdleMs - 1));
  TEST_ASSERT_TRUE(w.sleepDue(done + cfg.sessionIdleMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_command_waits_for_the_network_without_reading_or_uploading);
  RUN_TEST(test_wifi_command_syncs_the_time_then_reads_and_uploads_with_the_tag);
  RUN_TEST(test_wifi_command_offline_reads_and_reports_the_failed_upload);
  RUN_TEST(test_a_failed_time_sync_still_uploads_marked_unsynced);
  RUN_TEST(test_the_board_stays_awake_while_a_command_runs);
  return UNITY_END();
}
