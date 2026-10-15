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

void formatSleepLine(uint32_t timerS, char* out, size_t size) {
  if (size == 0) return;
  std::snprintf(out, size, "@sleep s=%lu", static_cast<unsigned long>(timerS));
}
