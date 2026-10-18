// Made-up readings for index.html?demo (and ?demo=offline), so the page can be
// tried without Supabase. Shaped like real_readings rows, newest first.
// Three days of 15-minute readings with a WATCH, an ALERT, a shock and a
// sensor error. The levels follow the rules only roughly: it's for looks.

export function demoReadings(now, { offline = false } = {}) {
  const STEP_MS = 15 * 60 * 1000;
  const COUNT = 3 * 24 * 4;
  const newest = now.getTime() - (offline ? 3 * 60 : 20) * 60 * 1000;

  let seed = 7;
  const noise = () => {
    seed = (seed * 1103515245 + 12345) % 2147483648;
    return (seed / 2147483648 - 0.5) * 0.06;
  };

  const rows = [];
  let level = "OK";
  let overWatch = 0;
  for (let i = 0; i < COUNT; i++) {
    const t = newest - (COUNT - 1 - i) * STEP_MS;
    const hours = i / 4;
    // Slow creep from 30 h to 40 h, a jump at 40 h, re-baselined at 46 h.
    let pitch = 0.05 + noise();
    let roll = -0.04 + noise();
    if (hours >= 30 && hours < 40) pitch += (hours - 30) * 0.14;
    if (hours >= 40 && hours < 46) pitch += 2.3;
    const tilt = Math.hypot(pitch, roll);
    const temp = 16 + 6 * Math.sin(((hours - 9) / 24) * 2 * Math.PI) + noise();

    let event = "none";
    if (i === COUNT - 30) event = "shock";
    const sensorError = i === COUNT - 60;
    if (sensorError) event = "sensor_error";

    if (!sensorError) {
      overWatch = tilt >= 1.0 ? overWatch + 1 : 0;
      if (tilt >= 2.0) level = "ALERT";
      else if (level === "ALERT" && tilt >= 1.9) level = "ALERT";
      else if (overWatch >= 2 || (level === "WATCH" && tilt >= 0.9)) level = "WATCH";
      else level = "OK";
    }

    const r2 = (v) => Math.round(v * 10000) / 10000;
    rows.push({
      ts: new Date(t).toISOString(),
      time_synced: true,
      trigger: event === "shock" ? "motion" : "scheduled",
      pitch_d: sensorError ? null : r2(pitch),
      roll_d: sensorError ? null : r2(roll),
      tilt_d: sensorError ? null : r2(tilt),
      temp_c: sensorError ? null : Math.round(temp * 100) / 100,
      level,
      event,
    });
  }
  return rows.reverse();
}
