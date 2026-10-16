#include "ReadingJson.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {

// Appends to a fixed buffer; remembers if anything didn't fit.
class Out {
 public:
  Out(char* buf, size_t size) : buf_(buf), size_(size) {
    if (size_ > 0) buf_[0] = '\0';
  }

  void printf(const char* fmt, ...) {
    if (full_) return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf_ + pos_, size_ - pos_, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= size_ - pos_) {
      full_ = true;
      return;
    }
    pos_ += static_cast<size_t>(n);
  }

  void put(char c) { printf("%c", c); }

  // A JSON string, or null for nullptr.
  void string(const char* s) {
    if (s == nullptr) {
      printf("null");
      return;
    }
    put('"');
    for (; *s; ++s) {
      if (*s == '"' || *s == '\\') {
        printf("\\%c", *s);
      } else if (static_cast<unsigned char>(*s) < 0x20) {
        printf("\\u%04x", static_cast<unsigned>(*s));
      } else {
        put(*s);
      }
    }
    put('"');
  }

  void number(const char* fmt, float v) {
    if (std::isnan(v) || std::isinf(v)) {
      printf("null");
    } else {
      printf(fmt, static_cast<double>(v));
    }
  }

  void key(const char* k) { printf("%s\"%s\":", pos_ > 1 ? "," : "", k); }

  // Length written, or 0 (and an empty buffer) if it didn't fit.
  size_t finish() {
    if (!full_) return pos_;
    if (size_ > 0) buf_[0] = '\0';
    return 0;
  }

 private:
  char* buf_;
  size_t size_;
  size_t pos_ = 0;
  bool full_ = false;
};

const char* triggerName(Trigger t) {
  switch (t) {
    case Trigger::Scheduled:
      return "scheduled";
    case Trigger::Motion:
      return "motion";
    case Trigger::Recheck:
      return "recheck";
    case Trigger::Event:
      return "event";
  }
  return "scheduled";
}

const char* eventName(ReadingEvent e) {
  switch (e) {
    case ReadingEvent::None:
      return "none";
    case ReadingEvent::Shock:
      return "shock";
    case ReadingEvent::SensorError:
      return "sensor_error";
  }
  return "none";
}

}  // namespace

void formatUtcIso8601(uint32_t utc, char* out, size_t size) {
  // Days to civil date: Howard Hinnant's civil_from_days, for days since 1970-01-01.
  const int64_t days = utc / 86400;
  const uint32_t secs = utc % 86400;
  const int64_t z = days + 719468;
  const int64_t era = z / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t day = doy - (153 * mp + 2) / 5 + 1;
  const int64_t month = mp < 10 ? mp + 3 : mp - 9;
  const int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  std::snprintf(out, size, "%04d-%02d-%02dT%02u:%02u:%02uZ", static_cast<int>(year),
                static_cast<int>(month), static_cast<int>(day), secs / 3600, secs / 60 % 60,
                secs % 60);
}

size_t encodeReadingJson(const Reading& r, const UploadHealth& h, char* out, size_t size) {
  Out o(out, size);
  char ts[24];
  formatUtcIso8601(r.ts, ts, sizeof ts);

  o.put('{');
  o.key("device_id");
  o.string(h.deviceId);
  o.key("seq");
  o.printf("%lu", static_cast<unsigned long>(r.seq));
  o.key("boot_count");
  o.printf("%lu", static_cast<unsigned long>(r.bootCount));
  o.key("ts");
  o.string(ts);
  o.key("time_synced");
  o.printf("%s", r.timeSynced ? "true" : "false");
  o.key("trigger");
  o.string(triggerName(r.trigger));
  // Angles to 0.0001 deg, finer than the 0.001 deg the @read line prints.
  o.key("pitch_d");
  o.number("%.4f", r.pitchDeg);
  o.key("roll_d");
  o.number("%.4f", r.rollDeg);
  o.key("tilt_d");
  o.number("%.4f", r.tiltDeg);
  o.key("pitch_raw");
  o.number("%.4f", r.pitchRaw);
  o.key("roll_raw");
  o.number("%.4f", r.rollRaw);
  o.key("temp_c");
  o.number("%.2f", r.tempC);
  o.key("noise");
  o.number("%.5f", r.noise);
  o.key("stable");
  o.printf("%s", r.stable ? "true" : "false");
  o.key("level");
  o.string(levelName(r.level));
  o.key("event");
  o.string(eventName(r.event));
  o.key("rssi");
  if (h.hasRssi) {
    o.printf("%d", h.rssi);
  } else {
    o.printf("null");
  }
  o.key("reset_reason");
  o.string(h.resetReason);
  o.key("fw");
  o.string(h.fwVersion);
  o.key("batt_v");
  o.printf("null");
  o.key("test_tag");
  if (r.testTag != 0) {
    o.printf("\"%lu\"", static_cast<unsigned long>(r.testTag));
  } else {
    o.printf("null");
  }
  o.put('}');
  return o.finish();
}
