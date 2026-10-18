#include "AlertMessage.h"

#include <cstdio>

#include "ReadingJson.h"
#include "config.h"

namespace {

void formatTime(uint32_t ts, char* out, size_t size) {
  if (isUtcValid(ts)) {
    char iso[24];
    formatUtcIso8601(ts, iso, sizeof iso);
    std::snprintf(out, size, "Time %s", iso);
  } else {
    std::snprintf(out, size, "Time unknown (%lu s after power-on)", static_cast<unsigned long>(ts));
  }
}

}  // namespace

void formatAlertMessage(const Alert& a, const char* deviceId, char* out, size_t size) {
  if (size == 0) return;
  char time[48];
  formatTime(a.ts, time, sizeof time);
  if (isTestTag(a.testTag)) {
    std::snprintf(out, size,
                  "%s\n"
                  "Test number %lu from %s\n"
                  "Current level %s, tilt delta %.2f deg\n"
                  "%s",
                  TEST_ALERT_DISCLAIMER, static_cast<unsigned long>(a.testTag), deviceId,
                  levelName(a.level), static_cast<double>(a.tiltDeg), time);
  } else {
    std::snprintf(out, size,
                  "Level %s at %s\n"
                  "Tilt delta %.2f deg\n"
                  "%s",
                  levelName(a.level), deviceId, static_cast<double>(a.tiltDeg), time);
  }
}

void formatAlertTitle(const Alert& a, const char* deviceId, char* out, size_t size) {
  if (size == 0) return;
  std::snprintf(out, size, "Landslide monitor %s: %s", deviceId,
                isTestTag(a.testTag) ? "TEST" : levelName(a.level));
}

int alertPriority(const Alert& a) {
  if (isTestTag(a.testTag)) return 3;
  switch (a.level) {
    case Level::Alert:
      return 5;
    case Level::Watch:
      return 4;
    case Level::Ok:
      return 3;
  }
  return 3;
}
