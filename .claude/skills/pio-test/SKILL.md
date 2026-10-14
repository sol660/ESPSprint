---
name: pio-test
description: Write and run Unity unit tests for the firmware with PlatformIO, on the dev PC (native) or on the ESP32 board. Use when the user asks to add tests, run tests, or verify module logic.
---

# Unit tests

PlatformIO uses the Unity framework. There are two kinds of suite, each a folder whose name starts with `test_`:

```
test/native/test_<name>/test_main.cpp     # PC, env: native   (logic, via fakes)
test/embedded/test_<name>/test_main.cpp   # board, env: esp32dev (real hardware)
```

## Native (PC) tests: the default

These test the wake-cycle core (`runWakeCycle`) through the fake ports in `test/native/fakes/Fakes.h`. No Arduino headers.

```cpp
#include <unity.h>

#include "fakes/Fakes.h"
#include "WakeCycle.h"

void setUp() {}
void tearDown() {}

void test_something() {
  FakeHardware hw;
  RtcState state;
  WakeResult r = runWakeCycle(WakeCause::Timer, state, CycleConfig{}, hw.ports());
  TEST_ASSERT_EQUAL_UINT32(15 * 60, r.nextWakeS);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_something);
  return UNITY_END();
}
```

```bash
~/.platformio/penv/Scripts/pio.exe test -e native 2>&1 | tail -40
```
Run one suite with `-f native/test_<name>`.

## Embedded (on-board) tests

```cpp
#include <Arduino.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_something() {
  bool found = /* e.g. probe the MPU6050 on I2C */ true;
  TEST_ASSERT_TRUE_MESSAGE(found, "MPU6050 not responding: check SDA21/SCL22 wiring");
}

void setup() {
  delay(2000);  // give the serial port time to connect
  UNITY_BEGIN();
  RUN_TEST(test_something);
  UNITY_END();
}

void loop() {}
```

Tests flash and run on the connected board:
```bash
~/.platformio/penv/Scripts/pio.exe test -e esp32dev 2>&1 | tail -40
```
Run one suite with `-f embedded/test_<name>`. To only check that tests compile without a board, add `--without-uploading --without-testing`.
