// On-board (#7): the offline buffer kept by Esp32Store (LittleFS) survives a
// real reboot, in order, and drops and wraps correctly. Boot 1 fills it and
// restarts; boot 2 runs the Unity checks. Uses its own namespace ("esptest"),
// so the device's real buffer is untouched.
#include <Arduino.h>
#include <esp_system.h>
#include <unity.h>

#include "Esp32Ports.h"

static constexpr uint32_t SAVED_MAGIC = 0xB0FFE7ED;
RTC_NOINIT_ATTR static uint32_t phase;  // survives esp_restart(), not power loss

static Esp32Store store("esptest");

static Reading readingNumber(uint32_t seq) {
  Reading r{};
  r.seq = seq;
  r.bootCount = 77;
  r.ts = 1792800000u + seq;
  r.tiltDeg = 0.5f * seq;
  return r;
}

void setUp() {}
void tearDown() {}

void test_buffer_survives_reboot_in_order() {
  TEST_ASSERT_EQUAL_MESSAGE(3, store.bufferCount(),
                            "offline buffer lost after reboot: LittleFS write or mount failed");
  for (size_t i = 0; i < 3; ++i) {
    Reading r{};
    TEST_ASSERT_TRUE_MESSAGE(store.bufferPeek(i, r), "buffered reading unreadable after reboot");
    TEST_ASSERT_EQUAL_UINT32(i + 1, r.seq);
    TEST_ASSERT_EQUAL_UINT32(77, r.bootCount);
    TEST_ASSERT_EQUAL_UINT32(1792800000u + i + 1, r.ts);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f * (i + 1), r.tiltDeg);
  }
}

void test_drop_then_push_keeps_the_order() {
  store.bufferDrop(2);
  TEST_ASSERT_TRUE(store.bufferPush(readingNumber(4)));
  TEST_ASSERT_EQUAL(2, store.bufferCount());
  Reading r{};
  TEST_ASSERT_TRUE(store.bufferPeek(0, r));
  TEST_ASSERT_EQUAL_UINT32(3, r.seq);
  TEST_ASSERT_TRUE(store.bufferPeek(1, r));
  TEST_ASSERT_EQUAL_UINT32(4, r.seq);
}

void test_clearing_empties_it() {
  store.bufferDrop(store.bufferCount());
  TEST_ASSERT_EQUAL(0, store.bufferCount());
  Reading r{};
  TEST_ASSERT_FALSE(store.bufferPeek(0, r));
}

void setup() {
  delay(2000);  // give the serial port time to connect
  if (phase != SAVED_MAGIC) {
    store.bufferDrop(store.bufferCount());  // start from empty
    for (uint32_t seq = 1; seq <= 3; ++seq) store.bufferPush(readingNumber(seq));
    phase = SAVED_MAGIC;
    esp_restart();
  }
  phase = 0;
  UNITY_BEGIN();
  RUN_TEST(test_buffer_survives_reboot_in_order);
  RUN_TEST(test_drop_then_push_keeps_the_order);
  RUN_TEST(test_clearing_empties_it);
  UNITY_END();
}

void loop() {}
