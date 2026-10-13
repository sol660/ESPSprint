#pragma once

// Central place for pin assignments and tunables.
// Keep hardware-specific numbers here, not scattered through src/.

#include <Arduino.h>

namespace config {

// Serial
constexpr uint32_t SERIAL_BAUD = 115200;

// Pins (ESP32 DevKit v1)
constexpr uint8_t PIN_LED = 2;  // onboard blue LED on most esp32dev boards

// Timing
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;

}  // namespace config
