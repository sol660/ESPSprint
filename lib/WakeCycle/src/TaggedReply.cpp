#include "TaggedReply.h"

#include <cstdarg>
#include <cstdio>

namespace {

// Appends the reading's keys; returns the new end position (clamped to size).
size_t appendReading(const Reading& r, char* out, size_t size, size_t pos) {
  if (pos >= size) return pos;
  const int n = std::snprintf(out + pos, size - pos,
                              " pitch=%.3f roll=%.3f tilt=%.3f stable=%d noise=%.5f temp=%.1f g=%.4f cal=%d",
                              r.pitchDeg, r.rollDeg, r.tiltDeg, r.stable ? 1 : 0, r.noise,
                              r.tempC, r.accelG, r.accelCalibrated ? 1 : 0);
  return n < 0 ? pos : pos + static_cast<size_t>(n);
}

// The `tag=` value: the test tag, or `none` for a real reading or alert.
struct TagText {
  char text[12] = "none";
  explicit TagText(uint32_t testTag) {
    if (isTestTag(testTag)) std::snprintf(text, sizeof text, "%lu", static_cast<unsigned long>(testTag));
  }
};

// printf-style appends to a fixed buffer, stopping (truncated) when it is full.
class Appender {
 public:
  Appender(char* out, size_t size) : out_(out), size_(size) {}
  void operator()(const char* fmt, ...) {
    if (pos_ >= size_) return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(out_ + pos_, size_ - pos_, fmt, args);
    va_end(args);
    if (n > 0) pos_ += static_cast<size_t>(n);
  }

 private:
  char* out_;
  size_t size_;
  size_t pos_ = 0;
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

void formatCalReply(const CalResult& r, char* out, size_t size) {
  if (size == 0) return;
  const char* step = r.step == CalStep::Save    ? "save"
                     : r.step == CalStep::Clear ? "clear"
                                                : calFaceName(r.face);
  Appender add(out, size);
  add("@cal ok=%d step=%s faces=%u", r.ok ? 1 : 0, step, static_cast<unsigned>(r.faceCount));
  if (r.step == CalStep::Face && r.hasMean) {
    add(" ax=%.4f ay=%.4f az=%.4f", static_cast<double>(r.mean[0]),
        static_cast<double>(r.mean[1]), static_cast<double>(r.mean[2]));
  }
  if (r.step == CalStep::Save && r.hasCalibration) {
    const AccelCalibration& c = r.calibration;
    add(" ox=%.4f oy=%.4f oz=%.4f sx=%.4f sy=%.4f sz=%.4f", static_cast<double>(c.offset[0]),
        static_cast<double>(c.offset[1]), static_cast<double>(c.offset[2]),
        static_cast<double>(c.scale[0]), static_cast<double>(c.scale[1]),
        static_cast<double>(c.scale[2]));
  }
  if (!r.ok) add(" err=%s", calErrorName(r.error));
  if (r.error == CalError::MissingFaces) {
    add(" missing=");
    bool first = true;
    for (int i = 0; i < CAL_FACE_COUNT; ++i) {
      if (!((r.missingFaces >> i) & 1u)) continue;
      add(first ? "%s" : ",%s", calFaceName(static_cast<CalFace>(i)));
      first = false;
    }
  }
  if (r.baselineCleared) add(" baseline=0");
}
