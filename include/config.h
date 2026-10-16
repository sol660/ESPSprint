#pragma once

// Central place for pin assignments and tunables (spec: issue #1).
// Keep hardware-specific numbers here, not scattered through src/.
// No Arduino includes: the wake-cycle core and its PC tests use this too.

#include <cstddef>
#include <cstdint>

namespace config {

constexpr const char* FW_VERSION = "0.1.0";

// The esp32dev_hil build (-DESPSPRINT_HIL_TIMINGS) is the same firmware with
// short timings, so board tests can watch real deep sleep in seconds (spec #14
// "Test build"). Only the values inside the #ifdef blocks below differ.

// Serial
constexpr uint32_t SERIAL_BAUD = 115200;
// Command window (spec #14): after power-on or reset the board listens for
// commands this long, then sleeps. Each command keeps it awake until
// SERIAL_SESSION_IDLE_MS after the last one. Timer wakes don't listen.
#ifdef ESPSPRINT_HIL_TIMINGS
constexpr uint32_t COMMAND_WINDOW_MS = 10 * 1000;
constexpr uint32_t SERIAL_SESSION_IDLE_MS = 20 * 1000;
#else
constexpr uint32_t COMMAND_WINDOW_MS = 60 * 1000;
constexpr uint32_t SERIAL_SESSION_IDLE_MS = 5 * 60 * 1000;
#endif

// Pins (fixed by the PCB)
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_MPU_INT = 34;   // jumpered from MPU6050 INT (H2 pin 12)
constexpr uint8_t PIN_CAM_WAKE = 19;  // to ESP32-CAM GPIO13

// MPU6050
constexpr uint8_t MPU_I2C_ADDR = 0x68;  // AD0 low
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint16_t IMU_SAMPLE_RATE_HZ = 100;  // readSample() is paced to this

// Wake schedule
#ifdef ESPSPRINT_HIL_TIMINGS
constexpr uint32_t NORMAL_INTERVAL_S = 30;
#else
constexpr uint32_t NORMAL_INTERVAL_S = 15 * 60;
#endif
constexpr uint32_t EVENT_INTERVAL_S = 60;        // reading cadence in event mode
constexpr uint32_t EVENT_DURATION_S = 30 * 60;   // event mode length after an ALERT
constexpr uint32_t RECHECK_DELAY_S = 5 * 60;     // after a shock

// Uploads
constexpr uint32_t UPLOAD_INTERVAL_S = 60 * 60;        // normal batching
constexpr uint32_t EVENT_UPLOAD_INTERVAL_S = 5 * 60;   // in event mode
constexpr uint32_t MOTION_UPLOAD_MIN_GAP_S = 10 * 60;  // cap on motion-triggered uploads
constexpr size_t BUFFER_CAPACITY = 3000;  // ~4 weeks of 15-min readings; oldest dropped

// Levels (applied to the compensated tilt delta, degrees). WATCH is 1.0 deg
// (spec #1: 0.5) until temperature calibration (ADR 0002).
constexpr float WATCH_DEG = 1.0f;
constexpr uint8_t WATCH_CONFIRM_READINGS = 2;  // readings in a row at or over WATCH_DEG
constexpr float ALERT_DEG = 2.0f;
constexpr float ALERT_STEP_DEG = 0.5f;     // change since previous reading
// A level drops only once the tilt is this far below its threshold, so a
// reading sitting at a threshold doesn't flap: WATCH holds down to 0.9 deg,
// ALERT down to 1.9 deg.
constexpr float LEVEL_DROP_MARGIN_DEG = 0.1f;
// ntfy server for alerts; the topic is secrets::NTFY_TOPIC.
constexpr const char* NTFY_SERVER = "https://ntfy.sh";

// Measurement
constexpr float STILL_ACCEL_STD_G = 0.003f;  // motion gate, tune on the bench
constexpr float STILL_GYRO_DPS = 1.0f;  // std dev within a window, so the zero-rate offset doesn't count
constexpr uint16_t WINDOW_SAMPLES = 50;  // samples per motion-gate window (Not set by spec: tune.)
constexpr uint8_t WINDOWS_PER_READING = 5;
constexpr uint32_t READING_TIME_LIMIT_MS = 10 * 1000;
// Baseline: a long average, captured on first boot or by the `baseline` command.
// (Not set by spec: tune. Must fit inside the watchdog with a reading.)
constexpr uint8_t BASELINE_WINDOWS = 20;
constexpr uint32_t BASELINE_TIME_LIMIT_MS = 25 * 1000;
constexpr float MOTION_WAKE_THRESHOLD_G = 0.1f;

// Shock photos are rate-limited so a windy night can't drain the battery.
// Gap and pulse length are not set by the spec: placeholders to confirm.
constexpr uint32_t SHOCK_PHOTO_MIN_GAP_S = 30 * 60;
constexpr uint32_t CAM_PULSE_MS = 50;

// Photo slots, local time. Night slots use the CAM flash LED.
struct PhotoSlot {
  uint8_t hour;
  uint8_t minute;
  bool flash;
};
constexpr PhotoSlot PHOTO_SLOTS[] = {
    {7, 0, false}, {12, 0, false}, {17, 0, false}, {22, 0, true}, {3, 0, true},
};
constexpr size_t PHOTO_SLOT_COUNT = sizeof(PHOTO_SLOTS) / sizeof(PHOTO_SLOTS[0]);
// POSIX TZ string for the site's local time (photo slots). TODO: set to the site's zone.
constexpr const char* TIMEZONE = "UTC0";

// Time limits
constexpr uint32_t WATCHDOG_READING_S = 45;   // reading-only wakes
constexpr uint32_t WATCHDOG_UPLOAD_S = 105;   // wakes that use WiFi
// WiFi (spec #14): home, then school, 30 s each. 2 x 30 s + NTP + HTTPS fits
// inside the 105 s upload watchdog.
constexpr uint32_t WIFI_CONNECT_TIMEOUT_S = 30;  // per network
constexpr uint8_t WIFI_MAX_NETWORKS = 2;  // the first 2 of secrets::WIFI_NETWORKS
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 5 * 1000;
// The clock counts as set once it is past this (2023-11-14): before the first
// sync after a power loss it counts from 1970.
constexpr uint32_t CLOCK_VALID_AFTER_UTC = 1700000000;
constexpr uint32_t HTTP_TIMEOUT_MS = 10 * 1000;
// Supabase REST endpoint for the readings table (supabase/schema.sql).
constexpr const char* SUPABASE_READINGS_PATH = "/rest/v1/readings";

}  // namespace config
