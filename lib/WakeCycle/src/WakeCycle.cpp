#include "WakeCycle.h"

WakeResult runWakeCycle(WakeCause cause, RtcState& state, const CycleConfig& cfg, Ports& ports) {
  (void)cause;
  (void)state;
  (void)ports;
  WakeResult result;
  result.nextWakeS = cfg.normalIntervalS;
  return result;
}
