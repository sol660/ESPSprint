#include <unity.h>

#include <cmath>

#include "WakeCycle.h"
#include "fakes/Fakes.h"

// Alerts waiting for ntfy are queued in flash, oldest first (code review of
// spec #14): a newer raise never overwrites one still waiting, only an
// escalation still waiting is folded into the higher level, and the queue
// drops its oldest only when full. A send whose clear fails is not repeated.

static CycleConfig testConfig() {
  CycleConfig cfg;
  cfg.windowSamples = 10;
  cfg.windowsPerReading = 5;
  cfg.baselineWindows = 10;
  return cfg;
}

// A board with a zero baseline, so the tilt delta is the board's own tilt.
struct Site {
  FakeHardware hw;
  RtcState state;
  CycleConfig cfg = testConfig();
  float pitch = 0;

  Site() {
    hw.store.hasBaseline = true;
    startBoot(WakeCause::PowerOn, state, hw.store);
  }

  // One timer wake with the board resting at pitchDeg.
  WakeResult wake(float pitchDeg) {
    pitch = pitchDeg;
    const float p = pitchDeg / 57.29577951f;
    const ImuSample s{-std::sin(p), 0, std::cos(p), 0, 0, 0};
    for (int i = 0; i < cfg.windowSamples * cfg.windowsPerReading; ++i) hw.imu.samples.push_back(s);
    return runWakeCycle(WakeCause::Timer, state, cfg, hw.ports());
  }

  // Wakes in 0.4 deg steps (under the 0.5 deg step rule) until it reaches toDeg.
  WakeResult walkTo(float toDeg) {
    WakeResult r;
    while (std::fabs(toDeg - pitch) > 0.001f) {
      const float d = toDeg - pitch;
      r = wake(pitch + (std::fabs(d) > 0.4f ? (d > 0 ? 0.4f : -0.4f) : d));
    }
    return r;
  }

  size_t alerts() const { return hw.uplink.alertsSent.size(); }
};

void setUp() {}
void tearDown() {}

void test_an_alert_still_waiting_is_not_overwritten_by_a_later_lower_one() {
  Site site;
  site.hw.uplink.up = false;
  TEST_ASSERT_EQUAL(Level::Alert, site.wake(2.4f).level);  // ALERT, not sent
  TEST_ASSERT_EQUAL(Level::Ok, site.walkTo(0.4f).level);   // drops back to OK
  site.walkTo(1.2f);
  TEST_ASSERT_EQUAL(Level::Watch, site.wake(1.2f).level);  // the 2nd over 1 deg: WATCH, not sent

  site.hw.uplink.up = true;
  WakeResult r = site.wake(1.2f);

  TEST_ASSERT_TRUE(r.alertSent);
  TEST_ASSERT_EQUAL(2, r.alertsSent);
  TEST_ASSERT_EQUAL(2, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);  // oldest first
  TEST_ASSERT_EQUAL(Level::Watch, site.hw.uplink.alertsSent[1].level);
  TEST_ASSERT_FALSE(site.hw.store.hasPendingAlert);

  r = site.wake(1.2f);  // all delivered: nothing again
  TEST_ASSERT_FALSE(r.alertTried);
  TEST_ASSERT_EQUAL(2, site.alerts());
}

void test_two_alerts_with_a_drop_between_are_both_delivered() {
  Site site;
  site.hw.uplink.up = false;
  site.wake(2.4f);       // ALERT
  site.walkTo(0.4f);     // OK
  TEST_ASSERT_EQUAL(Level::Alert, site.walkTo(2.4f).level);  // WATCH on the way, folded into ALERT

  site.hw.uplink.up = true;
  site.wake(2.4f);

  TEST_ASSERT_EQUAL(2, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[1].level);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.4f, site.hw.uplink.alertsSent[0].tiltDeg);
}

void test_a_full_queue_drops_its_oldest_alert() {
  Site site;
  site.cfg.alertQueueLen = 2;
  site.hw.uplink.up = false;
  site.wake(2.4f);    // ALERT (the oldest)
  site.walkTo(0.4f);
  site.walkTo(1.2f);
  WakeResult r = site.wake(1.2f);  // WATCH
  TEST_ASSERT_EQUAL(0, r.alertsDropped);
  site.walkTo(0.4f);
  r = site.walkTo(1.6f);  // WATCH again: the queue is full
  TEST_ASSERT_EQUAL(Level::Watch, r.level);
  TEST_ASSERT_EQUAL(1, r.alertsDropped);

  site.hw.uplink.up = true;
  site.wake(1.6f);

  TEST_ASSERT_EQUAL(2, site.alerts());
  TEST_ASSERT_EQUAL(Level::Watch, site.hw.uplink.alertsSent[0].level);
  TEST_ASSERT_EQUAL(Level::Watch, site.hw.uplink.alertsSent[1].level);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.6f, site.hw.uplink.alertsSent[1].tiltDeg);
}

void test_an_alert_sent_but_not_cleared_from_flash_is_not_sent_again() {
  Site site;
  site.hw.store.alertDropFails = true;

  WakeResult r = site.wake(2.4f);
  TEST_ASSERT_TRUE(r.alertSent);
  TEST_ASSERT_EQUAL(1, site.alerts());

  r = site.wake(2.4f);  // still in flash, but known to be delivered
  TEST_ASSERT_FALSE(r.alertTried);
  TEST_ASSERT_EQUAL(1, site.alerts());

  site.hw.store.alertDropFails = false;
  site.wake(2.4f);  // the clear is retried and works
  TEST_ASSERT_EQUAL(0, site.hw.store.alertCount());
  TEST_ASSERT_EQUAL(1, site.alerts());
}

void test_a_new_alert_after_an_uncleared_one_is_still_sent() {
  Site site;
  site.hw.store.alertDropFails = true;
  site.wake(2.4f);    // ALERT sent, clear failed
  site.walkTo(0.4f);  // OK
  site.walkTo(1.2f);
  site.wake(1.2f);  // WATCH raised

  TEST_ASSERT_EQUAL(2, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
  TEST_ASSERT_EQUAL(Level::Watch, site.hw.uplink.alertsSent[1].level);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_an_alert_still_waiting_is_not_overwritten_by_a_later_lower_one);
  RUN_TEST(test_two_alerts_with_a_drop_between_are_both_delivered);
  RUN_TEST(test_a_full_queue_drops_its_oldest_alert);
  RUN_TEST(test_an_alert_sent_but_not_cleared_from_flash_is_not_sent_again);
  RUN_TEST(test_a_new_alert_after_an_uncleared_one_is_still_sent);
  return UNITY_END();
}
