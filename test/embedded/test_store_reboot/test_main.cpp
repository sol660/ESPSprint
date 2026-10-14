// On-board: a baseline saved through Esp32Store survives a real reboot.
// Boot 1 saves and restarts; boot 2 runs the Unity check. Uses its own flash
// namespace so the device's real baseline is untouched.
#include <Arduino.h>
#include <esp_system.h>
#include <unity.h>

#include "Esp32Ports.h"

static constexpr uint32_t SAVED_MAGIC = 0x5AFEB007;
RTC_NOINIT_ATTR static uint32_t phase;  // survives esp_restart(), not power loss

static Esp32Store store("esptest");

void setUp() {}
void tearDown() {}

void test_baseline_survives_reboot() {
  Baseline b{};
  TEST_ASSERT_TRUE_MESSAGE(store.loadBaseline(b), "no baseline after reboot: flash (NVS) write lost");
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.5f, b.pitchDeg);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -3.25f, b.rollDeg);
}

void setup() {
  delay(2000);  // give the serial port time to connect
  if (phase != SAVED_MAGIC) {
    store.saveBaseline({12.5f, -3.25f});
    phase = SAVED_MAGIC;
    esp_restart();
  }
  phase = 0;
  UNITY_BEGIN();
  RUN_TEST(test_baseline_survives_reboot);
  UNITY_END();
}

void loop() {}
