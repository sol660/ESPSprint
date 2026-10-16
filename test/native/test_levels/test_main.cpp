#include <unity.h>

#include <cmath>

#include "WakeCycle.h"
#include "fakes/Fakes.h"

// Bare levels and ntfy alerts (#20; spec #14 "Levels (bare #8)", ADR 0002):
// WATCH at 1.0 deg on 2 readings in a row, ALERT at 2 deg or a 0.5 deg step,
// a margin before a level drops, and a notification only when the level goes up.

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

  Site() {
    hw.store.hasBaseline = true;
    startBoot(WakeCause::PowerOn, state, hw.store);
  }

  // One timer wake with the board resting at (pitch, roll) degrees.
  WakeResult wake(float pitchDeg, float rollDeg = 0.0f) {
    const CycleConfig cfg = testConfig();
    const float p = pitchDeg / 57.29577951f;
    const float r = rollDeg / 57.29577951f;
    // pitch = atan2(-x, sqrt(y^2 + z^2)), roll = atan2(y, z)
    const ImuSample s{-std::sin(p), std::cos(p) * std::sin(r), std::cos(p) * std::cos(r), 0, 0, 0};
    for (int i = 0; i < cfg.windowSamples * cfg.windowsPerReading; ++i) hw.imu.samples.push_back(s);
    return runWakeCycle(WakeCause::Timer, state, cfg, hw.ports());
  }

  size_t alerts() const { return hw.uplink.alertsSent.size(); }
};

void setUp() {}
void tearDown() {}

void test_one_reading_over_watch_stays_ok_and_two_in_a_row_raise_watch_with_a_notification() {
  Site site;

  WakeResult first = site.wake(1.2f);
  TEST_ASSERT_EQUAL(Level::Ok, first.level);
  TEST_ASSERT_EQUAL(Level::Ok, first.reading.level);
  TEST_ASSERT_EQUAL(0, site.alerts());

  WakeResult second = site.wake(1.3f);
  TEST_ASSERT_EQUAL(Level::Watch, second.level);
  TEST_ASSERT_EQUAL(Level::Watch, second.reading.level);
  TEST_ASSERT_TRUE(second.alertSent);
  TEST_ASSERT_EQUAL(1, site.alerts());
  const Alert& a = site.hw.uplink.alertsSent[0];
  TEST_ASSERT_EQUAL(Level::Watch, a.level);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.3f, a.tiltDeg);
  TEST_ASSERT_EQUAL_UINT32(second.reading.ts, a.ts);
}

void test_two_degrees_raises_alert_at_once_with_a_notification() {
  Site site;

  WakeResult r = site.wake(0.0f, 2.1f);

  TEST_ASSERT_EQUAL(Level::Alert, r.level);
  TEST_ASSERT_EQUAL(1, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.1f, site.hw.uplink.alertsSent[0].tiltDeg);
}

void test_a_half_degree_step_raises_alert_below_the_thresholds() {
  Site site;
  site.wake(0.2f);

  WakeResult r = site.wake(0.75f);  // under WATCH, but 0.55 deg since the last reading

  TEST_ASSERT_EQUAL(Level::Alert, r.level);
  TEST_ASSERT_EQUAL(1, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
}

void test_a_step_counts_movement_in_any_direction() {
  Site site;
  site.wake(0.3f);

  WakeResult r = site.wake(-0.3f);  // the same size of tilt, but it moved 0.6 deg

  TEST_ASSERT_EQUAL(Level::Alert, r.level);
}

void test_a_smaller_step_stays_ok() {
  Site site;
  site.wake(0.1f);

  WakeResult r = site.wake(0.5f);  // 0.4 deg

  TEST_ASSERT_EQUAL(Level::Ok, r.level);
  TEST_ASSERT_EQUAL(0, site.alerts());
}

void test_the_first_reading_after_a_cold_boot_has_no_step() {
  Site site;

  WakeResult r = site.wake(0.8f);  // 0.8 deg from zero, but there is no previous reading

  TEST_ASSERT_EQUAL(Level::Ok, r.level);
}

void test_readings_hovering_at_watch_do_not_flap() {
  Site site;
  site.wake(1.05f);
  site.wake(1.05f);  // WATCH

  const float hover[] = {0.95f, 1.02f, 0.92f, 1.0f, 0.97f};
  for (float tilt : hover) TEST_ASSERT_EQUAL(Level::Watch, site.wake(tilt).level);
  TEST_ASSERT_EQUAL(1, site.alerts());

  TEST_ASSERT_EQUAL(Level::Ok, site.wake(0.85f).level);  // past the margin: drops
  TEST_ASSERT_EQUAL(Level::Ok, site.wake(1.05f).level);  // WATCH again needs 2 in a row
  TEST_ASSERT_EQUAL(1, site.alerts());
}

void test_readings_hovering_at_alert_do_not_flap() {
  Site site;
  site.wake(1.8f);
  site.wake(2.05f);  // ALERT

  const float hover[] = {1.95f, 2.0f, 1.92f, 2.01f};
  for (float tilt : hover) TEST_ASSERT_EQUAL(Level::Alert, site.wake(tilt).level);
  TEST_ASSERT_EQUAL(1, site.alerts());

  WakeResult down = site.wake(1.85f);  // past the margin: drops to WATCH (still over 1.0)
  TEST_ASSERT_EQUAL(Level::Watch, down.level);
  TEST_ASSERT_EQUAL(1, site.alerts());  // no notification going down
}

void test_a_step_alert_drops_on_the_next_steady_reading() {
  Site site;
  site.wake(0.1f);
  site.wake(0.7f);  // step: ALERT

  WakeResult r = site.wake(0.72f);

  TEST_ASSERT_EQUAL(Level::Ok, r.level);
  TEST_ASSERT_EQUAL(1, site.alerts());
}

void test_no_repeat_notification_while_the_level_is_unchanged() {
  Site site;
  for (int i = 0; i < 5; ++i) site.wake(1.4f);
  TEST_ASSERT_EQUAL(1, site.alerts());  // WATCH once

  for (int i = 0; i < 5; ++i) site.wake(3.0f);
  TEST_ASSERT_EQUAL(2, site.alerts());  // then ALERT once
  TEST_ASSERT_EQUAL(Level::Watch, site.hw.uplink.alertsSent[0].level);
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[1].level);
}

void test_rising_again_after_a_drop_notifies_again() {
  Site site;
  site.wake(2.5f);  // ALERT
  site.wake(2.2f);
  TEST_ASSERT_EQUAL(Level::Watch, site.wake(1.85f).level);  // down, no notification
  TEST_ASSERT_EQUAL(1, site.alerts());

  WakeResult r = site.wake(2.3f);

  TEST_ASSERT_EQUAL(Level::Alert, r.level);
  TEST_ASSERT_EQUAL(2, site.alerts());
}

void test_the_level_goes_into_the_reading_and_its_upload() {
  Site site;
  site.wake(1.2f);
  WakeResult r = site.wake(1.2f);

  TEST_ASSERT_EQUAL(Level::Watch, r.reading.level);
  TEST_ASSERT_EQUAL(Level::Ok, site.hw.uplink.batchesSent.at(0).at(0).level);
  TEST_ASSERT_EQUAL(Level::Watch, site.hw.uplink.batchesSent.at(1).at(0).level);
}

void test_an_alert_that_failed_is_kept_and_retried_until_delivered_once() {
  Site site;
  site.hw.uplink.up = false;

  WakeResult r = site.wake(3.0f);
  TEST_ASSERT_TRUE(r.alertTried);
  TEST_ASSERT_FALSE(r.alertSent);
  TEST_ASSERT_TRUE(site.hw.store.hasPendingAlert);
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.store.pendingAlert.level);

  r = site.wake(3.0f);  // still offline: still waiting
  TEST_ASSERT_TRUE(r.alertTried);
  TEST_ASSERT_FALSE(r.alertSent);
  TEST_ASSERT_TRUE(site.hw.store.hasPendingAlert);

  site.hw.uplink.up = true;
  r = site.wake(3.0f);
  TEST_ASSERT_TRUE(r.alertSent);
  TEST_ASSERT_EQUAL(1, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
  TEST_ASSERT_FALSE(site.hw.store.hasPendingAlert);

  r = site.wake(3.0f);  // delivered once: not again
  TEST_ASSERT_FALSE(r.alertTried);
  TEST_ASSERT_EQUAL(1, site.alerts());
}

void test_an_alert_ntfy_refused_is_retried_on_the_next_wake() {
  Site site;
  site.hw.uplink.acceptsAlerts = false;  // WiFi up, ntfy refuses

  WakeResult r = site.wake(3.0f);
  TEST_ASSERT_FALSE(r.alertSent);
  TEST_ASSERT_TRUE(r.uploaded);  // the reading still goes up
  TEST_ASSERT_TRUE(site.hw.store.hasPendingAlert);

  site.hw.uplink.acceptsAlerts = true;
  r = site.wake(3.0f);
  TEST_ASSERT_TRUE(r.alertSent);
  TEST_ASSERT_EQUAL(1, site.alerts());
  TEST_ASSERT_EQUAL(2, site.hw.uplink.alertAttempts);
}

void test_a_pending_alert_is_retried_after_a_power_loss() {
  Site site;
  site.hw.uplink.up = false;
  site.wake(3.0f);

  site.hw.uplink.up = true;
  site.state = RtcState{};  // power lost: RTC memory gone, flash kept
  startBoot(WakeCause::PowerOn, site.state, site.hw.store);
  WakeResult r = site.wake(0.0f);

  TEST_ASSERT_TRUE(r.alertSent);
  TEST_ASSERT_EQUAL(1, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
}

void test_a_newer_alert_replaces_one_still_waiting() {
  Site site;
  site.hw.uplink.up = false;
  site.wake(1.2f);
  site.wake(1.3f);  // WATCH, not sent
  site.wake(2.5f);  // ALERT, not sent

  site.hw.uplink.up = true;
  site.wake(2.5f);

  TEST_ASSERT_EQUAL(1, site.alerts());
  TEST_ASSERT_EQUAL(Level::Alert, site.hw.uplink.alertsSent[0].level);
}

void test_the_alert_is_dated_back_with_its_reading_once_the_time_is_synced() {
  Site site;
  site.hw.clock.synced = false;
  site.hw.clock.utc = 42;
  site.hw.clock.utcAfterSync = 1792800000;

  WakeResult r = site.wake(3.0f);

  TEST_ASSERT_TRUE(r.reading.timeSynced);
  TEST_ASSERT_EQUAL_UINT32(r.reading.ts, site.hw.uplink.alertsSent.at(0).ts);
  TEST_ASSERT_EQUAL_UINT32(1792800000u, site.hw.uplink.alertsSent.at(0).ts);
}

void test_a_reading_with_no_still_window_changes_nothing() {
  Site site;
  site.hw.store.baseline = {-2.5f, 0.0f};  // a missing angle would look like 2.5 deg

  // No samples at all: not one still window.
  WakeResult r = runWakeCycle(WakeCause::Timer, site.state, testConfig(), site.hw.ports());

  TEST_ASSERT_FALSE(r.reading.stable);
  TEST_ASSERT_EQUAL(Level::Ok, r.level);
  TEST_ASSERT_EQUAL(0, site.alerts());
}

void test_the_thresholds_come_from_the_config() {
  CycleConfig cfg;
  TEST_ASSERT_EQUAL_FLOAT(1.0f, cfg.watchDeg);  // ADR 0002: 1.0 until calibration
  TEST_ASSERT_EQUAL(2, cfg.watchConfirmReadings);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, cfg.alertDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, cfg.alertStepDeg);
  TEST_ASSERT_EQUAL_FLOAT(0.1f, cfg.levelDropMarginDeg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_one_reading_over_watch_stays_ok_and_two_in_a_row_raise_watch_with_a_notification);
  RUN_TEST(test_two_degrees_raises_alert_at_once_with_a_notification);
  RUN_TEST(test_a_half_degree_step_raises_alert_below_the_thresholds);
  RUN_TEST(test_a_step_counts_movement_in_any_direction);
  RUN_TEST(test_a_smaller_step_stays_ok);
  RUN_TEST(test_the_first_reading_after_a_cold_boot_has_no_step);
  RUN_TEST(test_readings_hovering_at_watch_do_not_flap);
  RUN_TEST(test_readings_hovering_at_alert_do_not_flap);
  RUN_TEST(test_a_step_alert_drops_on_the_next_steady_reading);
  RUN_TEST(test_no_repeat_notification_while_the_level_is_unchanged);
  RUN_TEST(test_rising_again_after_a_drop_notifies_again);
  RUN_TEST(test_the_level_goes_into_the_reading_and_its_upload);
  RUN_TEST(test_an_alert_that_failed_is_kept_and_retried_until_delivered_once);
  RUN_TEST(test_an_alert_ntfy_refused_is_retried_on_the_next_wake);
  RUN_TEST(test_a_pending_alert_is_retried_after_a_power_loss);
  RUN_TEST(test_a_newer_alert_replaces_one_still_waiting);
  RUN_TEST(test_the_alert_is_dated_back_with_its_reading_once_the_time_is_synced);
  RUN_TEST(test_a_reading_with_no_still_window_changes_nothing);
  RUN_TEST(test_the_thresholds_come_from_the_config);
  return UNITY_END();
}
