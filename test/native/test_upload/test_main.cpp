#include <unity.h>

#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

// Upload, first build (spec #14): every reading is uploaded straight away. A
// failed upload is reported and the reading dropped (no buffer yet). The `wifi`
// command connects, takes a reading and uploads it with a test tag.

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

void test_every_reading_is_uploaded_straight_away() {
  FakeHardware hw;
  stillBoard(hw);
  RtcState state;
  startBoot(WakeCause::PowerOn, state, hw.store);

  WakeResult r = runWakeCycle(WakeCause::PowerOn, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(1, r.readingsSent);
  TEST_ASSERT_EQUAL(1, hw.uplink.batchesSent.size());
  TEST_ASSERT_EQUAL(1, hw.uplink.batchesSent[0].size());
  TEST_ASSERT_EQUAL_UINT32(r.reading.seq, hw.uplink.batchesSent[0][0].seq);
  TEST_ASSERT_EQUAL_UINT32(0, hw.uplink.batchesSent[0][0].testTag);
  TEST_ASSERT_FALSE(hw.uplink.connected);  // radio off again before the command window
}

void test_timer_wakes_upload_too() {
  FakeHardware hw;
  stillBoard(hw);
  RtcState state;

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(1, hw.uplink.batchesSent.size());
}

void test_no_network_drops_the_reading_without_buffering() {
  FakeHardware hw;
  stillBoard(hw);
  hw.uplink.up = false;
  RtcState state;

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_TRUE(r.tookReading);
  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_FALSE(r.uploaded);
  TEST_ASSERT_EQUAL(0, r.readingsSent);
  TEST_ASSERT_EQUAL(0, hw.store.buffer.size());
  TEST_ASSERT_EQUAL(1, hw.uplink.disconnects);
  TEST_ASSERT_EQUAL_UINT32(15 * 60, r.nextWakeS);  // sleeps as normal
}

void test_server_refusing_the_reading_drops_it() {
  FakeHardware hw;
  stillBoard(hw);
  hw.uplink.acceptsReadings = false;
  RtcState state;

  WakeResult r = runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.uploaded);
  TEST_ASSERT_EQUAL(0, hw.store.buffer.size());
  TEST_ASSERT_FALSE(hw.uplink.connected);
}

void test_reading_before_the_first_time_sync_is_marked_unsynced() {
  FakeHardware hw;
  stillBoard(hw);
  hw.clock.synced = false;
  hw.clock.syncSucceeds = false;
  hw.clock.utc = 42;  // seconds since power-on: no real time yet
  RtcState state;

  WakeResult r = runWakeCycle(WakeCause::PowerOn, state, testConfig(), hw.ports());

  TEST_ASSERT_FALSE(r.reading.timeSynced);
  TEST_ASSERT_EQUAL(1, hw.uplink.batchesSent.size());
  TEST_ASSERT_FALSE(hw.uplink.batchesSent[0][0].timeSynced);
  TEST_ASSERT_EQUAL_UINT32(42, hw.uplink.batchesSent[0][0].ts);
}

void test_time_is_synced_after_connecting_and_the_reading_dated_back() {
  FakeHardware hw;
  stillBoard(hw);
  hw.clock.synced = false;
  hw.clock.utc = 42;
  hw.clock.utcAfterSync = 1792800000;
  hw.clock.msPerUptimeRead = 3000;  // 3 s pass between the reading and the sync
  CycleConfig cfg = testConfig();
  cfg.readingTimeLimitMs = 60 * 1000;
  RtcState state;

  runWakeCycle(WakeCause::PowerOn, state, cfg, hw.ports());

  TEST_ASSERT_EQUAL(1, hw.clock.syncCalls);
  const Reading& sent = hw.uplink.batchesSent.at(0).at(0);
  TEST_ASSERT_TRUE(sent.timeSynced);
  // Taken before the sync, so dated back by the time since it was taken.
  TEST_ASSERT_EQUAL_UINT32(1792800000u - 3, sent.ts);
}

void test_an_already_synced_reading_keeps_its_time() {
  FakeHardware hw;
  stillBoard(hw);
  hw.clock.utc = 1792800000;
  hw.clock.utcAfterSync = 1792800005;
  RtcState state;

  runWakeCycle(WakeCause::Timer, state, testConfig(), hw.ports());

  TEST_ASSERT_EQUAL_UINT32(1792800000u, hw.uplink.batchesSent.at(0).at(0).ts);
}

void test_wifi_command_connects_then_reads_and_uploads_with_the_tag() {
  FakeHardware hw;
  stillBoard(hw);
  hw.clock.synced = false;
  hw.clock.utc = 42;
  hw.clock.utcAfterSync = 1792800000;
  RtcState state;
  startBoot(WakeCause::PowerOn, state, hw.store);

  CommandResult r = runCommand(Command::Wifi, state, testConfig(), hw.ports(), 123456789);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(1, hw.uplink.batchesSent.size());
  const Reading& sent = hw.uplink.batchesSent[0][0];
  TEST_ASSERT_EQUAL_UINT32(123456789, sent.testTag);
  TEST_ASSERT_EQUAL_UINT32(123456789, r.reading.testTag);
  // Read after connecting and syncing: real time, not dated back.
  TEST_ASSERT_TRUE(sent.timeSynced);
  TEST_ASSERT_EQUAL_UINT32(1792800000u, sent.ts);
  TEST_ASSERT_EQUAL_UINT32(1, sent.seq);
  TEST_ASSERT_EQUAL_UINT32(1, sent.bootCount);
  TEST_ASSERT_FALSE(hw.uplink.connected);
}

void test_wifi_command_offline_still_reads_and_reports_the_failure() {
  FakeHardware hw;
  stillBoard(hw);
  hw.uplink.up = false;
  RtcState state;

  CommandResult r = runCommand(Command::Wifi, state, testConfig(), hw.ports(), 5);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.reading.stable);
  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_FALSE(r.uploaded);
  TEST_ASSERT_EQUAL(0, hw.store.buffer.size());
}

void test_other_commands_do_not_use_wifi() {
  FakeHardware hw;
  stillBoard(hw);
  RtcState state;

  runCommand(Command::Read, state, testConfig(), hw.ports());
  runCommand(Command::Status, state, testConfig(), hw.ports());
  runCommand(Command::Baseline, state, testConfig(), hw.ports());

  TEST_ASSERT_EQUAL(0, hw.uplink.connects);
}

void test_upload_line_reports_the_result_tag_and_identity() {
  Reading r{};
  r.seq = 4;
  r.bootCount = 2;
  r.testTag = 123456789;
  char line[80];

  formatUploadLine(r, true, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@upload ok=1 tag=123456789 seq=4 boot=2", line);

  r.testTag = 0;
  formatUploadLine(r, false, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@upload ok=0 tag=none seq=4 boot=2", line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_reading_is_uploaded_straight_away);
  RUN_TEST(test_timer_wakes_upload_too);
  RUN_TEST(test_no_network_drops_the_reading_without_buffering);
  RUN_TEST(test_server_refusing_the_reading_drops_it);
  RUN_TEST(test_reading_before_the_first_time_sync_is_marked_unsynced);
  RUN_TEST(test_time_is_synced_after_connecting_and_the_reading_dated_back);
  RUN_TEST(test_an_already_synced_reading_keeps_its_time);
  RUN_TEST(test_wifi_command_connects_then_reads_and_uploads_with_the_tag);
  RUN_TEST(test_wifi_command_offline_still_reads_and_reports_the_failure);
  RUN_TEST(test_other_commands_do_not_use_wifi);
  RUN_TEST(test_upload_line_reports_the_result_tag_and_identity);
  return UNITY_END();
}
