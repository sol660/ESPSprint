#include <unity.h>

#include <cmath>
#include <cstring>

#include "ReadingJson.h"

// The reading encoder builds the JSON row the uplink posts to Supabase's
// readings table: every field from spec #1 plus test_tag (spec #14).

void setUp() {}
void tearDown() {}

static Reading realReading() {
  Reading r{};
  r.seq = 12;
  r.bootCount = 3;
  r.ts = 1792800000;  // 2026-10-24 00:00:00 UTC
  r.timeSynced = true;
  r.trigger = Trigger::Scheduled;
  r.pitchDeg = 0.1234f;
  r.rollDeg = -0.5f;
  r.tiltDeg = 0.515f;
  r.pitchRaw = 1.2f;
  r.rollRaw = -0.7f;
  r.tempC = 24.25f;
  r.noise = 0.00123f;
  r.stable = true;
  r.level = Level::Watch;
  r.event = ReadingEvent::None;
  return r;
}

static UploadHealth health() { return UploadHealth{"node-01", "0.1.0", "DEEPSLEEP", true, -61}; }

void test_a_real_reading_has_every_field_with_the_right_types() {
  char json[640];

  const size_t n = encodeReadingJson(realReading(), health(), json, sizeof json);

  TEST_ASSERT_EQUAL_STRING(
      "{\"device_id\":\"node-01\",\"seq\":12,\"boot_count\":3,"
      "\"ts\":\"2026-10-24T00:00:00Z\",\"time_synced\":true,\"trigger\":\"scheduled\","
      "\"pitch_d\":0.1234,\"roll_d\":-0.5000,\"tilt_d\":0.5150,"
      "\"pitch_raw\":1.2000,\"roll_raw\":-0.7000,\"temp_c\":24.25,\"noise\":0.00123,"
      "\"stable\":true,\"level\":\"WATCH\",\"event\":\"none\",\"rssi\":-61,"
      "\"reset_reason\":\"DEEPSLEEP\",\"fw\":\"0.1.0\",\"batt_v\":null,\"test_tag\":null}",
      json);
  TEST_ASSERT_EQUAL(std::strlen(json), n);
}

void test_a_tagged_unsynced_reading_with_missing_values_uses_nulls() {
  Reading r = realReading();
  r.testTag = 123456789;
  r.ts = 100;
  r.timeSynced = false;
  r.tempC = NAN;
  r.stable = false;
  r.trigger = Trigger::Motion;
  r.level = Level::Alert;
  r.event = ReadingEvent::Shock;
  UploadHealth h = health();
  h.hasRssi = false;
  h.resetReason = nullptr;
  char json[640];

  encodeReadingJson(r, h, json, sizeof json);

  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"test_tag\":\"123456789\"}"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"ts\":\"1970-01-01T00:01:40Z\",\"time_synced\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"temp_c\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"stable\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"trigger\":\"motion\""));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"level\":\"ALERT\",\"event\":\"shock\""));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"rssi\":null,\"reset_reason\":null"));
}

void test_other_enum_values_use_the_schema_vocabulary() {
  Reading r = realReading();
  r.trigger = Trigger::Recheck;
  r.level = Level::Ok;
  r.event = ReadingEvent::SensorError;
  char json[640];

  encodeReadingJson(r, health(), json, sizeof json);
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"trigger\":\"recheck\""));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"level\":\"OK\",\"event\":\"sensor_error\""));

  r.trigger = Trigger::Event;
  encodeReadingJson(r, health(), json, sizeof json);
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"trigger\":\"event\""));
}

void test_strings_are_escaped() {
  UploadHealth h = health();
  h.deviceId = "node\"1\\x";
  char json[640];

  encodeReadingJson(realReading(), h, json, sizeof json);

  TEST_ASSERT_NOT_NULL(std::strstr(json, "{\"device_id\":\"node\\\"1\\\\x\","));
}

void test_too_small_a_buffer_gives_nothing_rather_than_cut_json() {
  char json[100];

  const size_t n = encodeReadingJson(realReading(), health(), json, sizeof json);

  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_EQUAL_STRING("", json);
}

void test_utc_timestamps_cover_leap_days_and_the_century() {
  char ts[24];

  formatUtcIso8601(951868799, ts, sizeof ts);
  TEST_ASSERT_EQUAL_STRING("2000-02-29T23:59:59Z", ts);

  formatUtcIso8601(4102444799u, ts, sizeof ts);
  TEST_ASSERT_EQUAL_STRING("2099-12-31T23:59:59Z", ts);

  formatUtcIso8601(0, ts, sizeof ts);
  TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", ts);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_real_reading_has_every_field_with_the_right_types);
  RUN_TEST(test_a_tagged_unsynced_reading_with_missing_values_uses_nulls);
  RUN_TEST(test_other_enum_values_use_the_schema_vocabulary);
  RUN_TEST(test_strings_are_escaped);
  RUN_TEST(test_too_small_a_buffer_gives_nothing_rather_than_cut_json);
  RUN_TEST(test_utc_timestamps_cover_leap_days_and_the_century);
  return UNITY_END();
}
