#include "WakeCycle.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float RAD_TO_DEG_F = 57.29577951f;

struct Window {
  double mean[3];         // accelerometer, g
  double maxAccelStdG;    // largest per-axis std dev
  double maxGyroStdDps;
};

// Reads one window of samples, each corrected with `cal` when given; false if
// the IMU ran out before it filled.
bool readWindow(Imu& imu, uint16_t samples, const AccelCalibration* cal, Window& out) {
  double sum[6] = {0, 0, 0, 0, 0, 0};
  double sumSq[6] = {0, 0, 0, 0, 0, 0};
  for (uint16_t i = 0; i < samples; ++i) {
    ImuSample s;
    if (!imu.readSample(s)) return false;
    if (cal) {
      s.ax = (s.ax - cal->offset[0]) / cal->scale[0];
      s.ay = (s.ay - cal->offset[1]) / cal->scale[1];
      s.az = (s.az - cal->offset[2]) / cal->scale[2];
    }
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

// Averages accepted (still) windows until it has `windows` of them or runs out of
// time. Samples are corrected with `cal` when given. `meanOut`, when given, gets
// the averaged acceleration (g) of the accepted windows.
Reading measureTilt(const CycleConfig& cfg, uint8_t windows, uint32_t timeLimitMs, Imu& imu,
                    Clock& clock, const AccelCalibration* cal, double* meanOut = nullptr) {
  const uint32_t startMs = clock.uptimeMs();
  double sum[3] = {0, 0, 0};
  double noiseSum = 0;
  int accepted = 0;
  bool retried = false;
  bool sensorFailed = false;
  Window w;
  while (accepted < windows && clock.uptimeMs() - startMs < timeLimitMs) {
    if (!readWindow(imu, cfg.windowSamples, cal, w)) {
      // The MPU6050 stopped answering (#7): reset the I2C bus and try once
      // more. The window it broke off is thrown away.
      if (!retried) {
        retried = true;
        if (imu.resetBus()) continue;
      }
      sensorFailed = true;
      break;
    }
    if (w.maxAccelStdG > cfg.stillAccelStdG || w.maxGyroStdDps > cfg.stillGyroStdDps) continue;
    for (int k = 0; k < 3; ++k) sum[k] += w.mean[k];
    noiseSum += w.maxAccelStdG;
    ++accepted;
  }

  Reading r{};
  r.stable = accepted >= windows;
  // Still failing after the bus reset: the reading goes out as a sensor error
  // (any windows taken before the failure keep their angles).
  if (sensorFailed) r.event = ReadingEvent::SensorError;
  r.accelCalibrated = cal != nullptr;
  if (meanOut) {
    for (int k = 0; k < 3; ++k) meanOut[k] = accepted > 0 ? sum[k] / accepted : 0;
  }
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

// The stored accelerometer calibration, or nullptr when there is none.
const AccelCalibration* loadAccelCal(Store& store, AccelCalibration& buf) {
  return store.loadAccelCalibration(buf) ? &buf : nullptr;
}

// Long average; saved only if the board was still throughout.
bool captureBaseline(const CycleConfig& cfg, Ports& ports, Baseline& out) {
  AccelCalibration cal;
  const Reading b = measureTilt(cfg, cfg.baselineWindows, cfg.baselineTimeLimitMs, ports.imu,
                                ports.clock, loadAccelCal(ports.store, cal));
  if (!b.stable) return false;
  out = {b.pitchRaw, b.rollRaw};
  return ports.store.saveBaseline(out);
}

// Tilt delta against the baseline (the raw angle when there is none yet).
Reading readTiltDelta(const CycleConfig& cfg, Ports& ports, bool haveBaseline,
                      const Baseline& baseline) {
  AccelCalibration cal;
  Reading r = measureTilt(cfg, cfg.windowsPerReading, cfg.readingTimeLimitMs, ports.imu,
                          ports.clock, loadAccelCal(ports.store, cal));
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
// less the time since it was taken. It stays time_synced=false (#19 AC).
void dateBack(Clock& clock, uint32_t takenAtMs, Reading& r) {
  if (r.timeSynced || !clock.isSynced()) return;
  r.ts = clock.nowUtc() - (clock.uptimeMs() - takenAtMs) / 1000;
  // timeSynced stays false: the reading was taken before the first sync (#19 AC).
}

// The level rules (spec #1 "Levels", #14, ADR 0002), applied to each wake
// reading in turn. Returns the new level and updates the rules' memory.
//  - WATCH: tilt delta >= watchDeg on watchConfirmReadings readings in a row.
//  - ALERT: tilt delta >= alertDeg, or a step >= alertStepDeg since the previous
//    reading (the distance between the two (pitch, roll) deltas). Immediate.
//  - Dropping: a level is held until the tilt is levelDropMarginDeg below its
//    threshold (and nothing else raises it), then falls to whatever the
//    readings support; a step ALERT drops on the next reading without a step.
// A reading with no still window has no angle: it changes nothing.
Level classify(const Reading& r, const CycleConfig& cfg, LevelState& s) {
  if (r.accelG <= 0) return s.level;  // no accepted window: pitch/roll are not measured

  const float tilt = r.tiltDeg;
  bool step = false;
  if (s.hasPrevious) {
    const float dp = r.pitchDeg - s.prevPitchDeg;
    const float dr = r.rollDeg - s.prevRollDeg;
    step = std::sqrt(dp * dp + dr * dr) >= cfg.alertStepDeg;
  }
  s.hasPrevious = true;
  s.prevPitchDeg = r.pitchDeg;
  s.prevRollDeg = r.rollDeg;

  if (tilt >= cfg.watchDeg) {
    if (s.watchStreak < 255) ++s.watchStreak;
  } else {
    s.watchStreak = 0;
  }

  Level raised = Level::Ok;
  if (tilt >= cfg.alertDeg || step) {
    raised = Level::Alert;
  } else if (s.watchStreak >= cfg.watchConfirmReadings) {
    raised = Level::Watch;
  }

  Level held = Level::Ok;
  if (s.level == Level::Alert && tilt >= cfg.alertDeg - cfg.levelDropMarginDeg) {
    held = Level::Alert;
  } else if (s.level != Level::Ok && tilt >= cfg.watchDeg - cfg.levelDropMarginDeg) {
    held = Level::Watch;
  }

  s.level = std::max(raised, held);
  return s.level;
}

// Clears alerts ntfy took on an earlier wake whose clear from flash failed.
void clearDeliveredAlerts(RtcState& s, Store& store) {
  if (s.alertsUncleared == 0) return;
  const size_t count = store.alertCount();
  if (count < s.alertsUncleared) s.alertsUncleared = static_cast<uint8_t>(count);
  if (s.alertsUncleared > 0 && store.alertDrop(s.alertsUncleared)) s.alertsUncleared = 0;
}

// Adds a raised alert to the flash queue (oldest first), so every one is
// delivered. Only an escalation still waiting (WATCH, then ALERT before the
// WATCH went out) is folded into the higher level: the newest entry is
// replaced. When the queue is full the oldest is dropped. Returns how many
// undelivered alerts were dropped; queued says whether this one was saved.
size_t queueAlert(const Alert& a, RtcState& s, const CycleConfig& cfg, Store& store,
                  bool& queued) {
  const size_t count = store.alertCount();
  Alert newest{};
  if (count > s.alertsUncleared && store.alertPeek(count - 1, newest) && a.level > newest.level) {
    queued = store.alertReplaceNewest(a);
    return 0;
  }
  const size_t capacity = std::max<size_t>(cfg.alertQueueLen, 1);
  size_t lost = 0;
  if (count >= capacity) {
    const size_t drop = count - capacity + 1;
    if (store.alertDrop(drop)) {
      const size_t delivered = std::min<size_t>(drop, s.alertsUncleared);
      s.alertsUncleared = static_cast<uint8_t>(s.alertsUncleared - delivered);
      lost = drop - delivered;
    }
  }
  queued = store.alertPush(a);
  return lost;
}

// Sends the waiting alerts oldest first, stopping at the first failure so the
// order holds. Each is cleared from flash only once ntfy has it. If that clear
// fails they are remembered as delivered (RtcState::alertsUncleared) and
// cleared on a later wake instead of being sent again.
void sendQueuedAlerts(RtcState& s, Ports& ports, WakeResult& result) {
  const size_t skip = s.alertsUncleared;
  const size_t count = ports.store.alertCount();
  size_t sent = 0;
  Alert a{};
  while (skip + sent < count && ports.store.alertPeek(skip + sent, a)) {
    result.alert = a;
    if (!ports.uplink.sendAlert(a)) break;
    ++sent;
  }
  result.alertsSent = sent;
  result.alertSent = skip + sent == count;
  if (sent == 0) return;
  const size_t delivered = skip + sent;
  s.alertsUncleared = ports.store.alertDrop(delivered)
                          ? 0
                          : static_cast<uint8_t>(std::min<size_t>(delivered, UINT8_MAX));
}

// Keeps a reading that didn't reach the server in the offline buffer (#7). When
// the buffer is full the oldest readings make room. Returns how many were dropped.
size_t bufferReading(const Reading& r, const CycleConfig& cfg, Store& store) {
  const size_t count = store.bufferCount();
  size_t dropped = 0;
  if (count >= cfg.bufferCapacity) {
    dropped = count - cfg.bufferCapacity + 1;
    store.bufferDrop(dropped);
  }
  store.bufferPush(r);  // a failed save is logged by the Store
  return dropped;
}

// Sends the offline buffer, oldest first, then this wake's reading (needs the
// uplink connected). Each post carries up to cfg.uploadBatchSize readings; the
// buffered ones in it are dropped from flash only once the server has them, so
// a post that fails part way loses nothing. It stops at the first refused post
// and after cfg.uploadMaxBatchesPerWake posts, leaving the rest for the next
// wake; this wake's reading is then buffered after them, keeping the order.
// A reading the server took but whose drop from flash failed is sent again
// later: Supabase ignores a (device_id, seq, boot_count) it already has.
void uploadReadings(const Reading& reading, const CycleConfig& cfg, Ports& ports,
                    WakeResult& result) {
  Reading batch[config::UPLOAD_BATCH_SIZE];
  const size_t batchSize =
      std::max<size_t>(std::min<size_t>(cfg.uploadBatchSize, config::UPLOAD_BATCH_SIZE), 1);
  for (size_t posts = 0; posts < cfg.uploadMaxBatchesPerWake && !result.uploaded; ++posts) {
    const size_t waiting = ports.store.bufferCount();
    size_t fromBuffer = 0;
    while (fromBuffer < std::min(waiting, batchSize) &&
           ports.store.bufferPeek(fromBuffer, batch[fromBuffer])) {
      ++fromBuffer;
    }
    // This wake's reading goes in once everything before it is in the post (or
    // the buffer can't be read: then it goes up on its own rather than not at all).
    const bool withReading = fromBuffer < batchSize && (fromBuffer == waiting || fromBuffer == 0);
    size_t n = fromBuffer;
    if (withReading) batch[n++] = reading;
    if (!ports.uplink.sendReadings(batch, n)) return;
    result.readingsSent += n;
    result.uploaded = withReading;
    if (fromBuffer == 0) continue;
    ports.store.bufferDrop(fromBuffer);
    // A failed drop leaves them at the front: stop rather than send them again now.
    if (ports.store.bufferCount() != waiting - fromBuffer) return;
  }
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

  const Level before = state.levels.level;
  reading.level = result.level = classify(reading, cfg, state.levels);
  // A notification only when the level goes up (spec #1). It goes to the flash
  // queue first, so a WiFi failure (or a crash) can't swallow it.
  const bool raised = result.level > before;
  clearDeliveredAlerts(state, ports.store);
  Alert raisedAlert{result.level, reading.tiltDeg, reading.ts};
  bool queued = false;
  if (raised) result.alertsDropped = queueAlert(raisedAlert, state, cfg, ports.store, queued);
  // One that couldn't be saved (a flash error, logged by the Store) is still
  // tried in this wake, from memory.
  const bool unsaved = raised && !queued;
  const bool waiting = ports.store.alertPeek(state.alertsUncleared, result.alert);
  if (unsaved && !waiting) result.alert = raisedAlert;
  result.alertTried = waiting || unsaved;

  // Every reading is uploaded straight away (spec #14, first build). WiFi comes
  // after the reading, so the radio is off while measuring. On failure the
  // reading is dropped: there is no buffer yet.
  result.uploadTried = true;
  if (connectAndSync(ports)) {
    dateBack(ports.clock, takenAtMs, reading);
    if (raised && raisedAlert.ts != reading.ts) {
      raisedAlert.ts = reading.ts;  // dated back with its reading
      if (queued) ports.store.alertReplaceNewest(raisedAlert);
    }
    // Alerts first: they matter more than the reading.
    if (waiting) sendQueuedAlerts(state, ports, result);
    if (unsaved && (!waiting || result.alertSent)) {
      result.alert = raisedAlert;
      result.alertSent = ports.uplink.sendAlert(raisedAlert);
      if (result.alertSent) ++result.alertsSent;
    }
    uploadReadings(reading, cfg, ports, result);
  }
  ports.uplink.disconnect();
  if (!result.uploaded && cfg.bufferCapacity > 0) {
    result.bufferDropped = bufferReading(reading, cfg, ports.store);
  }
  result.readingsBuffered = ports.store.bufferCount();

  state.lastReading = reading;
  // Never a 0 s timer: the board must always wake again.
  result.nextWakeS = std::max<uint32_t>(cfg.normalIntervalS, 1);
  return result;
}

CycleConfig deviceCycleConfig() {
  CycleConfig cfg;
  cfg.bufferCapacity = config::BUFFER_CAPACITY;
  return cfg;
}

CommandWindow::CommandWindow(WakeCause cause, uint32_t nowMs, const CycleConfig& cfg)
    : fromMs_(nowMs), forMs_(cfg.commandWindowMs), idleMs_(cfg.sessionIdleMs) {
  // Only a person pressing EN or powering up is there to type: scheduled and
  // motion wakes stay cheap on power.
  if (cause == WakeCause::Timer || cause == WakeCause::Motion) forMs_ = idleMs_ = 0;
}

void CommandWindow::commandHandled(uint32_t nowMs) {
  running_ = false;
  const uint32_t elapsed = nowMs - fromMs_;
  const uint32_t remaining = elapsed >= forMs_ ? 0 : forMs_ - elapsed;
  if (remaining >= idleMs_) return;  // the window already runs longer than the idle time
  fromMs_ = nowMs;
  forMs_ = idleMs_;
}

bool CommandWindow::sleepDue(uint32_t nowMs) const {
  return !running_ && nowMs - fromMs_ >= forMs_;
}

CommandResult runCommand(Command cmd, RtcState& state, const CycleConfig& cfg, Ports& ports,
                         uint32_t testTag) {
  CommandResult result;
  switch (cmd) {
    case Command::Baseline:
      result.ok = captureBaseline(cfg, ports, result.baseline);
      result.hasBaseline = result.ok;
      // The step rule compares against the previous delta, which was taken
      // against the old baseline: start it again, or the change of baseline
      // would look like a sudden movement on the next wake.
      if (result.ok) state.levels.hasPrevious = false;
      break;
    case Command::Read:
      result.hasBaseline = ports.store.loadBaseline(result.baseline);
      result.reading = readTiltDelta(cfg, ports, result.hasBaseline, result.baseline);
      state.lastReading = result.reading;
      result.ok = true;
      break;
    case Command::Status:
      result.hasBaseline = ports.store.loadBaseline(result.baseline);
      result.hasAccelCalibration = ports.store.loadAccelCalibration(result.accelCalibration);
      result.reading = state.lastReading;
      result.bufferCount = ports.store.bufferCount();
      result.ok = true;
      break;
    case Command::ClearBuf: {
      const size_t before = ports.store.bufferCount();
      ports.store.bufferDrop(before);
      result.bufferCount = ports.store.bufferCount();
      result.bufferCleared = before - std::min(before, result.bufferCount);
      result.ok = result.bufferCount == 0;
      break;
    }
    case Command::Wifi:
    case Command::Alert: {
      WifiCommand wifi;
      wifi.start(cmd, testTag, ports);
      while (!wifi.poll(state, cfg, ports)) {
      }
      result = wifi.result();
      break;
    }
  }
  return result;
}

void WifiCommand::start(Command cmd, uint32_t testTag, Ports& ports) {
  cmd_ = cmd;
  testTag_ = testTag;
  result_ = CommandResult{};
  step_ = Step::Joining;
  ports.uplink.startConnect();
}

bool WifiCommand::poll(RtcState& state, const CycleConfig& cfg, Ports& ports) {
  switch (step_) {
    case Step::Idle:
    case Step::Done:
      return true;
    case Step::Joining:
      switch (ports.uplink.pollConnect()) {
        case Progress::Pending:
          return false;
        case Progress::Done:
          // Sync before reading, so the reading has real time (spec #14: NTP
          // after connecting).
          ports.clock.startSync();
          step_ = Step::Syncing;
          return false;
        case Progress::Failed:
          finish(false, state, cfg, ports);
          return true;
      }
      return false;
    case Step::Syncing:
      // A failed sync still uploads, with time_synced=false.
      if (ports.clock.pollSync() == Progress::Pending) return false;
      finish(true, state, cfg, ports);
      return true;
  }
  return true;
}

void WifiCommand::finish(bool connected, RtcState& state, const CycleConfig& cfg, Ports& ports) {
  CommandResult& r = result_;
  if (cmd_ == Command::Alert) {
    // The level the device is at and the latest reading's tilt, so the test
    // message also shows the current state. Not saved to flash: a test that
    // fails is reported now, never retried on later wakes.
    r.alert = Alert{state.levels.level, state.lastReading.tiltDeg, ports.clock.nowUtc(), testTag_};
    r.ok = true;
    r.alertTried = true;
    r.alertSent = connected && ports.uplink.sendAlert(r.alert);
    ports.uplink.disconnect();
    step_ = Step::Done;
    return;
  }
  r.hasBaseline = ports.store.loadBaseline(r.baseline);
  r.reading = readTiltDelta(cfg, ports, r.hasBaseline, r.baseline);
  stampReading(state, ports.clock, r.reading);
  r.reading.testTag = testTag_;
  r.reading.level = state.levels.level;  // the device's level; a command reading doesn't change it
  r.ok = true;
  r.uploadTried = true;
  r.uploaded = connected && ports.uplink.sendReadings(&r.reading, 1);
  ports.uplink.disconnect();
  state.lastReading = r.reading;
  step_ = Step::Done;
}

namespace {

constexpr const char* CAL_FACE_NAMES[CAL_FACE_COUNT] = {"+x", "-x", "+y", "-y", "+z", "-z"};

// Which axis a face points up (0 = x, 1 = y, 2 = z), and in which direction.
int calAxis(CalFace face) { return static_cast<int>(face) / 2; }
double calSign(CalFace face) { return static_cast<int>(face) % 2 == 0 ? 1.0 : -1.0; }

uint8_t countFaces(uint8_t faces) {
  uint8_t n = 0;
  for (int i = 0; i < CAL_FACE_COUNT; ++i) n += (faces >> i) & 1u;
  return n;
}

// Erases the baseline and restarts the step rule: both were against readings
// with the old correction (see the Baseline command).
bool restartBaseline(RtcState& state, Store& store) {
  state.levels.hasPrevious = false;
  return store.clearBaseline();
}

void captureCalFace(CalFace face, RtcState& state, const CycleConfig& cfg, Ports& ports,
                    CalResult& r) {
  double mean[3];
  // Raw samples (no correction): the calibration is worked out from them.
  const Reading m = measureTilt(cfg, cfg.baselineWindows, cfg.baselineTimeLimitMs, ports.imu,
                                ports.clock, nullptr, mean);
  r.hasMean = m.accelG > 0;
  for (int k = 0; k < 3; ++k) r.mean[k] = static_cast<float>(mean[k]);
  if (!m.stable) {
    r.error = CalError::Moving;
    return;
  }
  // The named axis must be the one pointing up: the largest, with the right sign.
  const int axis = calAxis(face);
  const double up = calSign(face) * mean[axis];
  for (int k = 0; k < 3; ++k) {
    if (up <= 0 || (k != axis && std::fabs(mean[k]) >= up)) {
      r.error = CalError::WrongFace;
      return;
    }
  }
  const int i = static_cast<int>(face);
  for (int k = 0; k < 3; ++k) state.cal.mean[i][k] = r.mean[k];
  state.cal.faces |= static_cast<uint8_t>(1u << i);
  r.ok = true;
}

void saveCalibration(RtcState& state, const CycleConfig& cfg, Store& store, CalResult& r) {
  const uint8_t all = (1u << CAL_FACE_COUNT) - 1;
  if (state.cal.faces != all) {
    r.error = CalError::MissingFaces;
    r.missingFaces = static_cast<uint8_t>(all & ~state.cal.faces);
    return;
  }
  AccelCalibration& c = r.calibration;
  bool inRange = true;
  for (int k = 0; k < 3; ++k) {
    const float up = state.cal.mean[2 * k][k];        // +axis face
    const float down = state.cal.mean[2 * k + 1][k];  // -axis face
    c.offset[k] = (up + down) / 2;
    c.scale[k] = (up - down) / 2;
    inRange = inRange && c.scale[k] >= cfg.calScaleMin && c.scale[k] <= cfg.calScaleMax &&
              std::fabs(c.offset[k]) <= cfg.calOffsetMaxG;
  }
  r.hasCalibration = true;
  if (!inRange) {
    r.error = CalError::OutOfRange;
    return;
  }
  if (!store.saveAccelCalibration(c)) {
    r.error = CalError::Flash;
    return;
  }
  r.baselineCleared = restartBaseline(state, store);
  state.cal = CalCapture{};
  r.ok = true;
}

}  // namespace

bool parseCalArg(const char* arg, CalStep& step, CalFace& face) {
  if (std::strcmp(arg, "save") == 0) {
    step = CalStep::Save;
    return true;
  }
  if (std::strcmp(arg, "clear") == 0) {
    step = CalStep::Clear;
    return true;
  }
  for (int i = 0; i < CAL_FACE_COUNT; ++i) {
    if (std::strcmp(arg, CAL_FACE_NAMES[i]) == 0) {
      step = CalStep::Face;
      face = static_cast<CalFace>(i);
      return true;
    }
  }
  return false;
}

const char* calFaceName(CalFace face) {
  const int i = static_cast<int>(face);
  return i < CAL_FACE_COUNT ? CAL_FACE_NAMES[i] : "?";
}

const char* calErrorName(CalError error) {
  switch (error) {
    case CalError::None:
      return "none";
    case CalError::Moving:
      return "moving";
    case CalError::WrongFace:
      return "wrong_face";
    case CalError::MissingFaces:
      return "missing_faces";
    case CalError::OutOfRange:
      return "out_of_range";
    case CalError::Flash:
      return "flash";
  }
  return "none";
}

CalResult runCalCommand(CalStep step, CalFace face, RtcState& state, const CycleConfig& cfg,
                        Ports& ports) {
  CalResult r;
  r.step = step;
  r.face = face;
  switch (step) {
    case CalStep::Face:
      captureCalFace(face, state, cfg, ports, r);
      break;
    case CalStep::Save:
      saveCalibration(state, cfg, ports.store, r);
      break;
    case CalStep::Clear:
      state.cal = CalCapture{};
      if (!ports.store.clearAccelCalibration()) {
        r.error = CalError::Flash;
        break;
      }
      r.baselineCleared = restartBaseline(state, ports.store);
      r.ok = true;
      break;
  }
  r.faceCount = countFaces(state.cal.faces);
  return r;
}
