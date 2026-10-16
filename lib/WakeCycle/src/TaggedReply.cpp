#include "TaggedReply.h"

#include <cstdio>

namespace {

// Appends the reading's keys; returns the new end position (clamped to size).
size_t appendReading(const Reading& r, char* out, size_t size, size_t pos) {
  if (pos >= size) return pos;
  const int n = std::snprintf(out + pos, size - pos,
                              " pitch=%.3f roll=%.3f tilt=%.3f stable=%d noise=%.5f temp=%.1f g=%.4f",
                              r.pitchDeg, r.rollDeg, r.tiltDeg, r.stable ? 1 : 0, r.noise,
                              r.tempC, r.accelG);
  return n < 0 ? pos : pos + static_cast<size_t>(n);
}

// The `tag=` value: the test tag, or `none` for a real reading or alert.
struct TagText {
  char text[12] = "none";
  explicit TagText(uint32_t testTag) {
    if (isTestTag(testTag)) std::snprintf(text, sizeof text, "%lu", static_cast<unsigned long>(testTag));
  }
};

}  // namespace

void formatReadReply(const CommandResult& r, char* out, size_t size) {
  if (size == 0) return;
  const int n = std::snprintf(out, size, "@read ok=%d", r.ok ? 1 : 0);
  if (n < 0) return;
  appendReading(r.reading, out, size, static_cast<size_t>(n));
}

void formatStatusReply(const CommandResult& r, char* out, size_t size) {
  if (size == 0) return;
  int n;
  if (r.hasBaseline) {
    n = std::snprintf(out, size, "@status ok=%d baseline=1 bpitch=%.3f broll=%.3f", r.ok ? 1 : 0,
                      r.baseline.pitchDeg, r.baseline.rollDeg);
  } else {
    n = std::snprintf(out, size, "@status ok=%d baseline=0", r.ok ? 1 : 0);
  }
  if (n < 0) return;
  appendReading(r.reading, out, size, static_cast<size_t>(n));
}

void formatBootLine(const BootInfo& b, char* out, size_t size) {
  if (size == 0) return;
  std::snprintf(out, size, "@boot fw=%s boot=%lu cause=%s seq=%lu", b.fwVersion,
                static_cast<unsigned long>(b.bootCount), wakeCauseName(b.cause),
                static_cast<unsigned long>(b.seq));
}

void formatUploadLine(const Reading& r, bool ok, char* out, size_t size) {
  if (size == 0) return;
  const TagText tag(r.testTag);
  std::snprintf(out, size, "@upload ok=%d tag=%s seq=%lu boot=%lu", ok ? 1 : 0, tag.text,
                static_cast<unsigned long>(r.seq), static_cast<unsigned long>(r.bootCount));
}

void formatSleepLine(uint32_t timerS, char* out, size_t size) {
  if (size == 0) return;
  std::snprintf(out, size, "@sleep s=%lu", static_cast<unsigned long>(timerS));
}

void formatAlertLine(const Alert& a, bool ok, char* out, size_t size) {
  if (size == 0) return;
  const TagText tag(a.testTag);
  std::snprintf(out, size, "@alert ok=%d tag=%s level=%s tilt=%.3f", ok ? 1 : 0, tag.text,
                levelName(a.level), static_cast<double>(a.tiltDeg));
}
