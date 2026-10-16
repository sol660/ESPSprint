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

// Numbers and dates a reading. (device_id, seq, boot_count) is its unique
// identity. Returns the uptime it was taken at, for dating it back later.
uint32_t stampReading(RtcState& state, Clock& clock, Reading& r) {
  r.seq = ++state.seq;
  r.bootCount = state.bootCount;
  r.ts = clock.nowUtc();
  r.timeSynced = clock.isSynced();
  return clock.uptimeMs();
}

// Joins a network and syncs the time (spec #14: NTP after connecting).
// A failed sync still uploads, with time_synced=false.
bool connectAndSync(Ports& ports) {
  if (!ports.uplink.connect()) return false;
  ports.clock.sync();
  return true;
}

// A reading taken before the clock was set gets real time once it is: now,
// less the time since it was taken.
void dateBack(Clock& clock, uint32_t takenAtMs, Reading& r) {
  if (r.timeSynced || !clock.isSynced()) return;
  r.ts = clock.nowUtc() - (clock.uptimeMs() - takenAtMs) / 1000;
  r.timeSynced = true;
}

}  // namespace

bool isColdBoot(WakeCause cause) {
  return cause != WakeCause::Timer && cause != WakeCause::Motion;
}

BootInfo startBoot(WakeCause cause, RtcState& state) {
  if (isColdBoot(cause)) {
    ++state.bootCount;
    state.seq = 0;
  }
  return {config::FW_VERSION, state.bootCount, cause, state.seq};
}

BootInfo startBoot(WakeCause cause, RtcState& state, Store& store) {
  if (!isColdBoot(cause)) return startBoot(cause, state);
  uint32_t stored = 0;
  state.bootCount = store.loadBootCount(stored) ? stored : 0;
  const BootInfo info = startBoot(cause, state);
  store.saveBootCount(info.bootCount);  // a failed save is logged by the Store
  return info;
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

  Reading& reading = result.reading;
  reading = readTiltDelta(cfg, ports, haveBaseline, baseline);
  const uint32_t takenAtMs = stampReading(state, ports.clock, reading);
  result.tookReading = true;

  // Every reading is uploaded straight away (spec #14, first build). WiFi comes
  // after the reading, so the radio is off while measuring. On failure the
  // reading is dropped: there is no buffer yet.
  result.uploadTried = true;
  if (connectAndSync(ports)) {
    dateBack(ports.clock, takenAtMs, reading);
    result.uploaded = ports.uplink.sendReadings(&reading, 1);
  }
  ports.uplink.disconnect();
  result.readingsSent = result.uploaded ? 1 : 0;

  state.lastReading = reading;
  result.nextWakeS = cfg.normalIntervalS;
  return result;
}

CommandWindow::CommandWindow(WakeCause cause, uint32_t nowMs, const CycleConfig& cfg)
    : fromMs_(nowMs), forMs_(cfg.commandWindowMs), idleMs_(cfg.sessionIdleMs) {
  // Only a person pressing EN or powering up is there to type: scheduled and
  // motion wakes stay cheap on power.
  if (cause == WakeCause::Timer || cause == WakeCause::Motion) forMs_ = idleMs_ = 0;
}

void CommandWindow::commandHandled(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - fromMs_;
  const uint32_t remaining = elapsed >= forMs_ ? 0 : forMs_ - elapsed;
  if (remaining >= idleMs_) return;  // the window already runs longer than the idle time
  fromMs_ = nowMs;
  forMs_ = idleMs_;
}

bool CommandWindow::sleepDue(uint32_t nowMs) const { return nowMs - fromMs_ >= forMs_; }

CommandResult runCommand(Command cmd, RtcState& state, const CycleConfig& cfg, Ports& ports,
                         uint32_t testTag) {
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
    case Command::Wifi: {
      // Connect first, so the reading has real time, then read and upload.
      const bool connected = connectAndSync(ports);
      result.hasBaseline = ports.store.loadBaseline(result.baseline);
      result.reading = readTiltDelta(cfg, ports, result.hasBaseline, result.baseline);
      stampReading(state, ports.clock, result.reading);
      result.reading.testTag = testTag;
      result.ok = true;
      result.uploadTried = true;
      result.uploaded = connected && ports.uplink.sendReadings(&result.reading, 1);
      ports.uplink.disconnect();
      state.lastReading = result.reading;
      break;
    }
  }
  return result;
}
