#include <unity.h>

#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

// Offline buffer (#7): a reading that can't be uploaded is kept in flash and
// sent, oldest first, ahead of the new reading at the next upload that works.
// When the buffer is full the oldest readings are dropped. Supabase ignores a
// re-sent (device_id, seq, boot_count), so a reading may be sent twice but is
// never stored twice; these tests check nothing is lost and nothing the server
// already took is sent again in the normal case.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  cfg.bufferCapacity = 100;
  return cfg;
}

// The server takes `acceptBatches` posts, then refuses every one after
// (-1: takes all).
class FlakyUplink : public FakeUplink {
 public:
  int acceptBatches = -1;
  bool sendReadings(const Reading* readings, size_t count) override {
    if (acceptBatches == 0) return false;
    if (!FakeUplink::sendReadings(readings, count)) return false;
    if (acceptBatches > 0) --acceptBatches;
    return true;
  }
};

// A flash whose buffer drops fail silently (the readings stay).
class StuckDropStore : public FakeStore {
 public:
  bool dropFails = false;
  void bufferDrop(size_t count) override {
    if (!dropFails) FakeStore::bufferDrop(count);
  }
};

// A still, level board with a zero baseline.
struct Site {
  FakeHardware hw;
  FlakyUplink uplink;
  StuckDropStore store;
  Ports ports{hw.imu, hw.clock, store, uplink, hw.cam, hw.sleeper};
  RtcState state;
  CycleConfig cfg = testConfig();

  Site() {
    store.hasBaseline = true;
    startBoot(WakeCause::PowerOn, state, store);
  }

  WakeResult wake() {
    for (int i = 0; i < cfg.windowSamples * cfg.windowsPerReading; ++i) {
      hw.imu.samples.push_back({0, 0, 1, 0, 0, 0});
    }
    return runWakeCycle(WakeCause::Timer, state, cfg, ports);
  }

  // Every reading the server took, in the order it took them.
  std::vector<uint32_t> sentSeqs() const {
    std::vector<uint32_t> seqs;
    for (const auto& batch : uplink.batchesSent) {
      for (const Reading& r : batch) seqs.push_back(r.seq);
    }
    return seqs;
  }

  std::vector<uint32_t> bufferedSeqs() const {
    std::vector<uint32_t> seqs;
    for (const Reading& r : store.buffer) seqs.push_back(r.seq);
    return seqs;
  }
};

static void assertSeqs(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual) {
  TEST_ASSERT_EQUAL_MESSAGE(expected.size(), actual.size(), "number of readings");
  for (size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_UINT32(expected[i], actual[i]);
}

void setUp() {}
void tearDown() {}

void test_a_reading_that_cannot_be_uploaded_is_buffered() {
  Site site;
  site.uplink.up = false;

  WakeResult r = site.wake();

  TEST_ASSERT_TRUE(r.uploadTried);
  TEST_ASSERT_FALSE(r.uploaded);
  TEST_ASSERT_EQUAL(0, r.readingsSent);
  assertSeqs({1}, site.bufferedSeqs());
  TEST_ASSERT_EQUAL(1, r.readingsBuffered);
}

void test_a_reading_the_server_refuses_is_buffered() {
  Site site;
  site.uplink.acceptsReadings = false;

  WakeResult r = site.wake();

  TEST_ASSERT_FALSE(r.uploaded);
  assertSeqs({1}, site.bufferedSeqs());
}

void test_when_the_uplink_is_back_the_buffer_is_sent_in_order_and_emptied() {
  Site site;
  site.uplink.up = false;
  site.wake();
  site.wake();
  site.wake();
  assertSeqs({1, 2, 3}, site.bufferedSeqs());

  site.uplink.up = true;
  WakeResult r = site.wake();

  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(4, r.readingsSent);
  assertSeqs({1, 2, 3, 4}, site.sentSeqs());  // oldest first, the new reading last
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
  TEST_ASSERT_EQUAL(0, r.readingsBuffered);

  site.wake();  // nothing left over: only the new reading
  assertSeqs({1, 2, 3, 4, 5}, site.sentSeqs());
}

void test_buffered_readings_keep_their_own_identity_and_time() {
  Site site;
  site.uplink.up = false;
  site.hw.clock.utc = 1792800000;
  site.wake();
  site.hw.clock.utc = 1792800900;
  site.uplink.up = true;

  site.wake();

  const Reading& old = site.uplink.batchesSent.at(0).at(0);
  TEST_ASSERT_EQUAL_UINT32(1, old.seq);
  TEST_ASSERT_EQUAL_UINT32(1, old.bootCount);
  TEST_ASSERT_EQUAL_UINT32(1792800000u, old.ts);
}

void test_a_full_buffer_drops_the_oldest_readings_first() {
  Site site;
  site.cfg.bufferCapacity = 3;
  site.uplink.up = false;
  site.wake();
  site.wake();
  WakeResult r = site.wake();
  TEST_ASSERT_EQUAL(0, r.bufferDropped);

  r = site.wake();
  TEST_ASSERT_EQUAL(1, r.bufferDropped);
  r = site.wake();
  TEST_ASSERT_EQUAL(1, r.bufferDropped);
  TEST_ASSERT_EQUAL(3, r.readingsBuffered);
  assertSeqs({3, 4, 5}, site.bufferedSeqs());

  site.uplink.up = true;
  site.wake();
  assertSeqs({3, 4, 5, 6}, site.sentSeqs());
}

void test_an_upload_that_fails_part_way_loses_and_duplicates_nothing() {
  Site site;
  site.cfg.uploadBatchSize = 2;
  site.uplink.up = false;
  for (int i = 0; i < 4; ++i) site.wake();  // seq 1-4 buffered

  site.uplink.up = true;
  site.uplink.acceptBatches = 1;  // the first post goes through, the second fails
  WakeResult r = site.wake();     // seq 5

  TEST_ASSERT_FALSE(r.uploaded);
  TEST_ASSERT_EQUAL(2, r.readingsSent);
  assertSeqs({1, 2}, site.sentSeqs());
  assertSeqs({3, 4, 5}, site.bufferedSeqs());  // the rest, still in order

  site.uplink.acceptBatches = -1;
  r = site.wake();  // seq 6

  TEST_ASSERT_TRUE(r.uploaded);
  assertSeqs({1, 2, 3, 4, 5, 6}, site.sentSeqs());  // each once, in order
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
}

void test_a_long_backlog_is_sent_over_several_wakes() {
  Site site;
  site.cfg.uploadBatchSize = 2;
  site.cfg.uploadMaxBatchesPerWake = 2;
  site.uplink.up = false;
  for (int i = 0; i < 5; ++i) site.wake();  // seq 1-5 buffered

  site.uplink.up = true;
  WakeResult r = site.wake();  // seq 6: only two posts this wake

  TEST_ASSERT_EQUAL(2, site.uplink.batchesSent.size());
  TEST_ASSERT_EQUAL(4, r.readingsSent);
  TEST_ASSERT_FALSE(r.uploaded);
  assertSeqs({5, 6}, site.bufferedSeqs());  // the new reading waits behind the rest

  r = site.wake();  // seq 7
  TEST_ASSERT_TRUE(r.uploaded);
  assertSeqs({1, 2, 3, 4, 5, 6, 7}, site.sentSeqs());
  TEST_ASSERT_EQUAL(0, r.readingsBuffered);
}

void test_readings_sent_but_not_cleared_from_flash_are_kept_and_sent_again_later() {
  Site site;
  site.cfg.uploadBatchSize = 2;
  site.uplink.up = false;
  for (int i = 0; i < 3; ++i) site.wake();  // seq 1-3

  site.uplink.up = true;
  site.store.dropFails = true;
  WakeResult r = site.wake();  // seq 4

  // The first post went through but couldn't be cleared: it stops there, and
  // nothing is lost (Supabase ignores the copies sent later).
  assertSeqs({1, 2}, site.sentSeqs());
  TEST_ASSERT_FALSE(r.uploaded);
  assertSeqs({1, 2, 3, 4}, site.bufferedSeqs());

  site.store.dropFails = false;
  site.wake();  // seq 5
  assertSeqs({1, 2, 1, 2, 3, 4, 5}, site.sentSeqs());
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
}

void test_the_device_runs_with_the_offline_buffer() {
  TEST_ASSERT_EQUAL(config::BUFFER_CAPACITY, deviceCycleConfig().bufferCapacity);
  TEST_ASSERT_EQUAL(3000, config::BUFFER_CAPACITY);
}

void test_clearbuf_empties_the_buffer_and_says_how_many_went() {
  Site site;
  site.uplink.up = false;
  for (int i = 0; i < 3; ++i) site.wake();
  const int connects = site.uplink.connects;

  CommandResult r = runCommand(Command::ClearBuf, site.state, site.cfg, site.ports);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL(3, r.bufferCleared);
  TEST_ASSERT_EQUAL(0, r.bufferCount);
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
  TEST_ASSERT_EQUAL(connects, site.uplink.connects);  // no WiFi for the command
}

void test_clearbuf_reports_a_flash_error() {
  Site site;
  site.uplink.up = false;
  site.wake();
  site.store.dropFails = true;

  CommandResult r = runCommand(Command::ClearBuf, site.state, site.cfg, site.ports);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL(1, r.bufferCount);
}

void test_status_shows_how_many_readings_are_buffered() {
  Site site;
  site.uplink.up = false;
  site.wake();
  site.wake();

  CommandResult r = runCommand(Command::Status, site.state, site.cfg, site.ports);

  TEST_ASSERT_EQUAL(2, r.bufferCount);
}

void test_buffer_and_clearbuf_lines() {
  char line[80];
  formatBufferLine(12, 4, 1, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@buffer count=12 sent=4 dropped=1", line);

  CommandResult r;
  r.ok = true;
  r.bufferCleared = 7;
  r.bufferCount = 0;
  formatClearBufLine(r, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@clearbuf ok=1 cleared=7 count=0", line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_reading_that_cannot_be_uploaded_is_buffered);
  RUN_TEST(test_a_reading_the_server_refuses_is_buffered);
  RUN_TEST(test_when_the_uplink_is_back_the_buffer_is_sent_in_order_and_emptied);
  RUN_TEST(test_buffered_readings_keep_their_own_identity_and_time);
  RUN_TEST(test_a_full_buffer_drops_the_oldest_readings_first);
  RUN_TEST(test_an_upload_that_fails_part_way_loses_and_duplicates_nothing);
  RUN_TEST(test_a_long_backlog_is_sent_over_several_wakes);
  RUN_TEST(test_readings_sent_but_not_cleared_from_flash_are_kept_and_sent_again_later);
  RUN_TEST(test_the_device_runs_with_the_offline_buffer);
  RUN_TEST(test_clearbuf_empties_the_buffer_and_says_how_many_went);
  RUN_TEST(test_clearbuf_reports_a_flash_error);
  RUN_TEST(test_status_shows_how_many_readings_are_buffered);
  RUN_TEST(test_buffer_and_clearbuf_lines);
  return UNITY_END();
}
