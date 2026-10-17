#include <unity.h>

#include <set>

#include "TaggedReply.h"
#include "WakeCycle.h"
#include "fakes/Fakes.h"

// A reading the server refuses for its content (#7 follow-up, user decision
// 2026-10-17): the refused post is retried one reading at a time to find the
// bad one, so good readings still go up. A reading refused on
// config::BUFFER_MAX_REFUSALS separate wakes is dropped from the buffer
// (logged), so one bad row can't hold up the rest. Network failures never
// count, and nor does a wake where the server took nothing at all (it is then
// refusing everything, e.g. a schema mismatch, so no single reading is to blame).

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

// The server refuses (HTTP 4xx) any post holding one of `bad` from boot
// `badBoot`, or every post when `refuseAll`. Counts every post it is sent.
class RefusingUplink : public FakeUplink {
 public:
  std::set<uint32_t> bad;  // seq numbers it refuses
  uint32_t badBoot = 1;
  bool refuseAll = false;
  int posts = 0;
  bool refused = false;
  bool sendReadings(const Reading* readings, size_t count) override {
    refused = false;
    if (!connected) return false;
    ++posts;
    bool ok = !refuseAll;
    for (size_t i = 0; i < count && ok; ++i) ok = readings[i].bootCount != badBoot || bad.count(readings[i].seq) == 0;
    if (!ok) {
      refused = true;
      return false;
    }
    return FakeUplink::sendReadings(readings, count);
  }
  bool lastSendRefused() override { return refused; }
};

// A store that can rewrite its oldest reading (the refusal count).
class CountingStore : public FakeStore {
 public:
  bool bufferReplaceOldest(const Reading& r) override {
    if (buffer.empty()) return false;
    buffer.front() = r;
    return true;
  }
};

struct Site {
  FakeHardware hw;
  RefusingUplink uplink;
  CountingStore store;
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

  // Wakes offline until seq 1..n are buffered.
  void bufferOffline(int n) {
    uplink.up = false;
    for (int i = 0; i < n; ++i) wake();
    uplink.up = true;
  }

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

void test_a_reading_refused_on_three_wakes_is_skipped_and_the_rest_go_up() {
  Site site;
  site.bufferOffline(4);  // seq 1-4
  site.uplink.bad = {2};

  WakeResult r = site.wake();  // seq 5
  // 1 goes up on its own, 2 is the bad one and is held; the new reading still goes up.
  assertSeqs({1, 5}, site.sentSeqs());
  assertSeqs({2, 3, 4}, site.bufferedSeqs());
  TEST_ASSERT_TRUE(r.uploaded);
  TEST_ASSERT_EQUAL(0, r.bufferSkipped);

  r = site.wake();  // seq 6: refused a 2nd time
  TEST_ASSERT_EQUAL(0, r.bufferSkipped);
  assertSeqs({2, 3, 4}, site.bufferedSeqs());

  r = site.wake();  // seq 7: refused a 3rd time: skipped, and the rest go up
  TEST_ASSERT_EQUAL(1, r.bufferSkipped);
  TEST_ASSERT_TRUE(r.uploaded);
  // 7 went up on its own first (it showed the server takes good readings).
  assertSeqs({1, 5, 6, 7, 3, 4}, site.sentSeqs());
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
}

void test_refusals_are_counted_once_per_wake_not_per_retry() {
  Site site;
  site.bufferOffline(3);
  site.uplink.bad = {1};

  site.wake();  // the post of 1-4 is refused, then 1 on its own: one wake

  TEST_ASSERT_EQUAL_UINT32(1, site.store.buffer.front().seq);
  TEST_ASSERT_EQUAL_UINT8(1, site.store.buffer.front().uploadRefusals);
}

void test_the_refusal_count_is_kept_in_flash_across_a_power_loss() {
  Site site;
  site.bufferOffline(2);
  site.uplink.bad = {1};
  site.wake();
  site.wake();  // refused twice

  site.state = RtcState{};  // power lost: RTC memory gone, flash kept
  startBoot(WakeCause::PowerOn, site.state, site.store);
  WakeResult r = site.wake();

  TEST_ASSERT_EQUAL(1, r.bufferSkipped);
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
}

void test_when_the_server_refuses_everything_nothing_is_skipped() {
  Site site;
  site.bufferOffline(3);
  site.uplink.refuseAll = true;  // e.g. a schema mismatch in the live project

  WakeResult r;
  for (int i = 0; i < 5; ++i) r = site.wake();

  TEST_ASSERT_EQUAL(0, r.bufferSkipped);
  assertSeqs({1, 2, 3, 4, 5, 6, 7, 8}, site.bufferedSeqs());
  for (const Reading& b : site.store.buffer) TEST_ASSERT_EQUAL_UINT8(0, b.uploadRefusals);
}

void test_a_network_failure_never_counts_as_a_refusal() {
  Site site;
  site.bufferOffline(2);
  site.uplink.acceptsReadings = false;  // fails, but not a refusal of the content

  for (int i = 0; i < 4; ++i) site.wake();

  TEST_ASSERT_EQUAL(6, site.store.buffer.size());
  for (const Reading& b : site.store.buffer) TEST_ASSERT_EQUAL_UINT8(0, b.uploadRefusals);
}

void test_finding_the_bad_reading_keeps_to_the_posts_per_wake_cap() {
  Site site;
  site.cfg.uploadMaxBatchesPerWake = 3;
  site.bufferOffline(10);
  site.uplink.bad = {7};

  WakeResult r = site.wake();  // seq 11

  TEST_ASSERT_EQUAL(3, site.uplink.posts);  // the refused post of 1-10 and 11, then 1 and 2
  assertSeqs({1, 2}, site.sentSeqs());
  TEST_ASSERT_FALSE(r.uploaded);
  assertSeqs({3, 4, 5, 6, 7, 8, 9, 10, 11}, site.bufferedSeqs());
}

void test_a_bad_new_reading_is_buffered_and_skipped_later() {
  Site site;
  site.uplink.bad = {1};

  site.wake();  // seq 1 refused on its own: nothing else went up, so no count yet
  assertSeqs({1}, site.bufferedSeqs());
  TEST_ASSERT_EQUAL_UINT8(0, site.store.buffer.front().uploadRefusals);

  site.wake();  // 2 goes up, so 1 is counted
  site.wake();
  WakeResult r = site.wake();

  TEST_ASSERT_EQUAL(1, r.bufferSkipped);
  assertSeqs({2, 3, 4}, site.sentSeqs());
  TEST_ASSERT_EQUAL(0, site.store.buffer.size());
}

void test_the_buffer_line_gains_skipped() {
  char line[80];
  formatBufferLine(12, 4, 1, 2, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("@buffer count=12 sent=4 dropped=1 skipped=2", line);
}

void test_the_device_skips_after_three_refusals() {
  TEST_ASSERT_EQUAL(3, config::BUFFER_MAX_REFUSALS);
  TEST_ASSERT_EQUAL(config::BUFFER_MAX_REFUSALS, CycleConfig{}.bufferMaxRefusals);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_reading_refused_on_three_wakes_is_skipped_and_the_rest_go_up);
  RUN_TEST(test_refusals_are_counted_once_per_wake_not_per_retry);
  RUN_TEST(test_the_refusal_count_is_kept_in_flash_across_a_power_loss);
  RUN_TEST(test_when_the_server_refuses_everything_nothing_is_skipped);
  RUN_TEST(test_a_network_failure_never_counts_as_a_refusal);
  RUN_TEST(test_finding_the_bad_reading_keeps_to_the_posts_per_wake_cap);
  RUN_TEST(test_a_bad_new_reading_is_buffered_and_skipped_later);
  RUN_TEST(test_the_buffer_line_gains_skipped);
  RUN_TEST(test_the_device_skips_after_three_refusals);
  return UNITY_END();
}
