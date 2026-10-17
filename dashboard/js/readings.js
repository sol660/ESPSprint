// Pure logic on rows of the real_readings view (supabase/schema.sql): no DOM,
// no network, so dashboard/test/ can check it in Node.

// The device counts as having a real clock once ts is past this
// (config::CLOCK_VALID_AFTER_UTC, 2023-11-14). Earlier rows were taken before
// the first time sync and their ts counts from power-on, so they're left out.
const CLOCK_VALID_AFTER_MS = 1700000000 * 1000;

const tsMs = (r) => Date.parse(r.ts);

// Rows with a usable timestamp, oldest first.
export function usableReadings(rows) {
  return rows
    .filter((r) => {
      const t = tsMs(r);
      return Number.isFinite(t) && t > CLOCK_VALID_AFTER_MS;
    })
    .sort((a, b) => tsMs(a) - tsMs(b));
}

// Series for the two charts, oldest first, as { x: ms since 1970, y } points.
// A missing value (sensor error) is y: null, which the chart draws as a gap.
// watch/alert are flat lines across the loaded time range.
export function chartData(rows, cfg) {
  const usable = usableReadings(rows);
  const series = (key) => usable.map((r) => ({ x: tsMs(r), y: r[key] ?? null }));
  const span = usable.length ? [tsMs(usable[0]), tsMs(usable[usable.length - 1])] : [];
  const flat = (y) => span.map((x) => ({ x, y }));
  return {
    pitch: series("pitch_d"),
    roll: series("roll_d"),
    tilt: series("tilt_d"),
    temp: series("temp_c"),
    watch: flat(cfg.watchDeg),
    alert: flat(cfg.alertDeg),
  };
}

// The events list, newest first. Each event is
//   { kind: "level", ts, from, to, tilt }        level differs from the reading before
//   { kind: "shock" | "sensor_error", ts, tilt } from the event column
// The first loaded reading never counts as a level change: what came before
// it isn't loaded.
export function eventsFrom(rows) {
  const usable = usableReadings(rows);
  const events = [];
  usable.forEach((r, i) => {
    const tilt = r.tilt_d ?? null;
    const prev = usable[i - 1];
    if (prev && prev.level !== r.level) {
      events.push({ kind: "level", ts: r.ts, from: prev.level, to: r.level, tilt });
    }
    if (r.event === "shock" || r.event === "sensor_error") {
      events.push({ kind: r.event, ts: r.ts, tilt });
    }
  });
  return events.reverse();
}

// What the status card and the OFFLINE badge show.
// offline: no reading, or the newest is more than offlineAfterMinutes old.
export function statusOf(rows, now, cfg) {
  const usable = usableReadings(rows);
  const latest = usable.length ? usable[usable.length - 1] : null;
  if (!latest) return { latest: null, offline: true, ageMinutes: null };
  const ageMinutes = (now.getTime() - tsMs(latest)) / 60000;
  return { latest, offline: ageMinutes > cfg.offlineAfterMinutes, ageMinutes };
}
