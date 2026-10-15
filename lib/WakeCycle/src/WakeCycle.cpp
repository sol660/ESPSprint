#include "WakeCycle.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float RAD_TO_DEG_F = 57.29577951f;

struct Window {
  double mean[3];         // accelerometer, g
  double maxAccelStdG;    // largest per-axis std dev
  double maxGyroStdDps;
};

// Reads one window of samples; false if the IMU ran out before it filled.
bool readWindow(Imu& imu, uint16_t samples, Window& out) {
  double sum[6] = {0, 0, 0, 0, 0, 0};
  double sumSq[6] = {0, 0, 0, 0, 0, 0};
  for (uint16_t i = 0; i < samples; ++i) {
    ImuSample s;
    if (!imu.readSample(s)) return false;
    const double v[6] = {s.ax, s.ay, s.az, s.gx, s.gy, s.gz};
    for (int k = 0; k < 6; ++k) {
      sum[k] += v[k];
      sumSq[k] += v[k] * v[k];
    }
  }
  out.maxAccelStdG = 0;
  out.maxGyroStdDps = 0;
  for (int k = 0; k < 6; ++k) {
    const double mean = sum[k] / samples;
    const double sd = std::sqrt(std::max(sumSq[k] / samples - mean * mean, 0.0));
    if (k < 3) {
      out.mean[k] = mean;
      out.maxAccelStdG = std::max(out.maxAccelStdG, sd);
    } else {
      out.maxGyroStdDps = std::max(out.maxGyroStdDps, sd);
    }
  }
  return true;
}

// Averages accepted (still) windows until it has `windows` of them or runs out of time.
Reading measureTilt(const CycleConfig& cfg, uint8_t windows, uint32_t timeLimitMs, Imu& imu,
                    Clock& clock) {
  const uint32_t startMs = clock.uptimeMs();
  double sum[3] = {0, 0, 0};
  double noiseSum = 0;
  int accepted = 0;
  Window w;
  while (accepted < windows &&
         clock.uptimeMs() - startMs < timeLimitMs &&
         readWindow(imu, cfg.windowSamples, w)) {
    if (w.maxAccelStdG > cfg.stillAccelStdG || w.maxGyroStdDps > cfg.stillGyroStdDps) continue;
    for (int k = 0; k < 3; ++k) sum[k] += w.mean[k];
    noiseSum += w.maxAccelStdG;
    ++accepted;
  }

  Reading r{};
  r.stable = accepted >= windows;
  if (accepted > 0) {
    r.noise = static_cast<float>(noiseSum / accepted);
    const double x = sum[0], y = sum[1], z = sum[2];  // direction only, so no need to divide
    r.accelG = static_cast<float>(std::sqrt(x * x + y * y + z * z) / accepted);
    r.pitchRaw = static_cast<float>(std::atan2(-x, std::sqrt(y * y + z * z))) * RAD_TO_DEG_F;
    r.rollRaw = static_cast<float>(std::atan2(y, z)) * RAD_TO_DEG_F;
  }
  r.pitchDeg = r.pitchRaw;
  r.rollDeg = r.rollRaw;
  return r;
}

// Long average; saved only if the board was still throughout.
bool captureBaseline(const CycleConfig& cfg, Ports& ports, Baseline& out) {
  const Reading b =
      measureTilt(cfg, cfg.baselineWindows, cfg.baselineTimeLimitMs, ports.imu, ports.clock);
  if (!b.stable) return false;
  out = {b.pitchRaw, b.rollRaw};
  return ports.store.saveBaseline(out);
}

// Tilt delta against the baseline (the raw angle when there is none yet).
Reading readTiltDelta(const CycleConfig& cfg, Ports& ports, bool haveBaseline,
                      const Baseline& baseline) {
  Reading r =
      measureTilt(cfg, cfg.windowsPerReading, cfg.readingTimeLimitMs, ports.imu, ports.clock);
  if (haveBaseline) {
    r.pitchDeg = r.pitchRaw - baseline.pitchDeg;
    r.rollDeg = r.rollRaw - baseline.rollDeg;
  }
  r.tiltDeg = std::sqrt(r.pitchDeg * r.pitchDeg + r.rollDeg * r.rollDeg);
  if (!ports.imu.readTemperatureC(r.tempC)) r.tempC = NAN;
  return r;
}

}  // namespace

BootInfo startBoot(WakeCause cause, RtcState& state) {
  ++state.bootCount;
  return {config::FW_VERSION, state.bootCount, cause};
}

const char* wakeCauseName(WakeCause cause) {
  switch (cause) {
    case WakeCause::PowerOn:
      return "PowerOn";
    case WakeCause::Timer:
      return "Timer";
    case WakeCause::Motion:
      return "Motion";
    case WakeCause::Serial:
      return "Serial";
  }
  return "Unknown";
}

WakeResult runWakeCycle(WakeCause cause, RtcState& state, const CycleConfig& cfg, Ports& ports) {
  (void)cause;
  WakeResult result;
  Baseline baseline{};
  bool haveBaseline = ports.store.loadBaseline(baseline);
  if (!haveBaseline) haveBaseline = captureBaseline(cfg, ports, baseline);

  result.reading = readTiltDelta(cfg, ports, haveBaseline, baseline);
  state.lastReading = result.reading;
  result.tookReading = true;
  result.nextWakeS = cfg.normalIntervalS;
  return result;
}

CommandResult runCommand(Command cmd, RtcState& state, const CycleConfig& cfg, Ports& ports) {
  CommandResult result;
  switch (cmd) {
    case Command::Baseline:
      result.ok = captureBaseline(cfg, ports, result.baseline);
      result.hasBaseline = result.ok;
      break;
    case Command::Read:
      result.hasBaseline = ports.store.loadBaseline(result.baseline);
      result.reading = readTiltDelta(cfg, ports, result.hasBaseline, result.baseline);
      state.lastReading = result.reading;
      result.ok = true;
      break;
    case Command::Status:
      result.hasBaseline = ports.store.loadBaseline(result.baseline);
      result.reading = state.lastReading;
      result.ok = true;
      break;
  }
  return result;
}
