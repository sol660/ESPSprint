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

// What the status card and the OFFLINE badge show.
// offline: no reading, or the newest is more than offlineAfterMinutes old.
export function statusOf(rows, now, cfg) {
  const usable = usableReadings(rows);
  const latest = usable.length ? usable[usable.length - 1] : null;
  if (!latest) return { latest: null, offline: true, ageMinutes: null };
  const ageMinutes = (now.getTime() - tsMs(latest)) / 60000;
  return { latest, offline: ageMinutes > cfg.offlineAfterMinutes, ageMinutes };
}
