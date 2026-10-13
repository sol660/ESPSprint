---
name: pio-test
description: Write and run Unity unit tests for the firmware with PlatformIO, on the ESP32 board. Use when the user asks to add tests, run tests, or verify module logic.
---

# Unit tests

PlatformIO uses the Unity framework. Each test suite is a folder in `test/` whose name starts with `test_`:

```
test/test_<module>/test_main.cpp
```

## Test file shape

```cpp
#include <Arduino.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_something() {
  TEST_ASSERT_EQUAL(4, 2 + 2);
}

void setup() {
  delay(2000);  // give the serial port time to connect
  UNITY_BEGIN();
  RUN_TEST(test_something);
  UNITY_END();
}

void loop() {}
```

## Run

Tests run on the connected board (requires hardware, flashes it):
```bash
~/.platformio/penv/Scripts/pio.exe test -e esp32dev 2>&1 | tail -40
```
Run one suite with `-f test_<module>`. To only check that tests compile without a board: add `--without-uploading --without-testing`.
