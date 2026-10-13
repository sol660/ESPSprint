---
name: esp32-module
description: Scaffold a new self-contained firmware module (sensor, actuator, WiFi, display, state machine...) in lib/ following this project's conventions. Use when adding a new feature or hardware component to the ESP32 firmware.
---

# Create a module

Each feature lives in its own folder under `lib/` so PlatformIO compiles it as a library and it can be unit-tested.

```
lib/<ModuleName>/
  src/<ModuleName>.h
  src/<ModuleName>.cpp
```

## Conventions

- A class with `begin()` (called from `setup()`, returns `bool` success) and `update()` (called every `loop()`, must be **non-blocking** — no `delay()`, use `millis()` timing).
- Pins and tunables come in through the constructor or from `include/config.h`; no magic numbers in the module.
- Log with the Arduino-ESP32 log macros so verbosity follows `CORE_DEBUG_LEVEL`:
  ```cpp
  log_i("started on pin %d", pin_);   // log_e / log_w / log_i / log_d / log_v
  ```
- Keep hardware access thin and logic separate, so logic can be tested in `test/`.
- Avoid `String` in hot paths and avoid heap allocation in `update()` (fragmentation on long-running boards).

## Template

```cpp
// lib/Blinker/src/Blinker.h
#pragma once
#include <Arduino.h>

class Blinker {
 public:
  Blinker(uint8_t pin, uint32_t intervalMs) : pin_(pin), intervalMs_(intervalMs) {}
  bool begin();
  void update();

 private:
  uint8_t pin_;
  uint32_t intervalMs_;
  uint32_t lastMs_ = 0;
  bool on_ = false;
};
```

After creating it: include it in `src/main.cpp`, wire `begin()`/`update()`, then run the `pio-build` skill.
