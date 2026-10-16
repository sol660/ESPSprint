#pragma once

// The ntfy notification text for an Alert (spec #1 story 19: level, tilt delta
// and time; spec #14 story 52: test messages say so). Hardware-free, so the
// uplink adapter and the PC tests share it.

#include <cstddef>

#include "Ports.h"

// The first line of every test message. Board tests look for it, with the number.
constexpr const char* TEST_ALERT_DISCLAIMER = "TEST \xE2\x80\x94 not a real alert";  // em dash, UTF-8

// The message body (UTF-8). A real alert:
//   Level <OK|WATCH|ALERT> at <device>
//   Tilt delta <x.xx> deg
//   Time <ISO 8601 UTC>   (or "Time unknown (<n> s after power-on)" before the first sync)
// A test (testTag != 0) starts with TEST_ALERT_DISCLAIMER and the number.
void formatAlertMessage(const Alert& a, const char* deviceId, char* out, size_t size);

// `Landslide monitor <device>: <level>`, or `...: TEST`. ASCII (an HTTP header).
void formatAlertTitle(const Alert& a, const char* deviceId, char* out, size_t size);

// ntfy priority 1-5: ALERT 5 (urgent), WATCH 4 (high), a test or OK 3 (default).
int alertPriority(const Alert& a);
