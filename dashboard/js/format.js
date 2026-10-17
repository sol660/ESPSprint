// Text formatting for the page. Pure, so it can be tested in Node.

// "just now", "42 min", "3 h 10 min", "2 d 2 h".
export function formatAge(minutes) {
  const m = Math.floor(minutes);
  if (m < 1) return "just now";
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  if (h < 24) return m % 60 ? `${h} h ${m % 60} min` : `${h} h`;
  const d = Math.floor(h / 24);
  return h % 24 ? `${d} d ${h % 24} h` : `${d} d`;
}

// "12.34°", or "–" for a missing value (sensor error).
export function deg(v, digits = 2) {
  return v == null ? "–" : `${v.toFixed(digits)}°`;
}

// A timestamp in the viewer's local time, e.g. "Sat 17 Oct, 14:05".
export function localTime(ts) {
  return new Date(ts).toLocaleString(undefined, {
    weekday: "short", day: "numeric", month: "short", hour: "2-digit", minute: "2-digit",
  });
}
