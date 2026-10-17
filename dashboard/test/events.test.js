// Events list (issue #9): level changes worked out from consecutive readings,
// shocks and sensor errors from the event column. Newest first.
import { test } from "node:test";
import assert from "node:assert/strict";
import { eventsFrom } from "../js/readings.js";

function row(minute, level, extra = {}) {
  const ts = new Date(Date.UTC(2026, 9, 17, 10, minute)).toISOString();
  return { ts, level, event: "none", tilt_d: 0.2, ...extra };
}

test("steady OK readings: no events", () => {
  assert.deepEqual(eventsFrom([row(0, "OK"), row(15, "OK"), row(30, "OK")]), []);
});

test("OK -> WATCH -> ALERT -> OK: three level changes, newest first", () => {
  const rows = [
    row(0, "OK"),
    row(15, "WATCH", { tilt_d: 1.1 }),
    row(30, "WATCH", { tilt_d: 1.3 }),
    row(31, "ALERT", { tilt_d: 2.4 }),
    row(45, "OK", { tilt_d: 0.3 }),
  ];
  const ev = eventsFrom(rows);
  assert.deepEqual(
    ev.map((e) => [e.kind, e.from, e.to, e.ts, e.tilt]),
    [
      ["level", "ALERT", "OK", "2026-10-17T10:45:00.000Z", 0.3],
      ["level", "WATCH", "ALERT", "2026-10-17T10:31:00.000Z", 2.4],
      ["level", "OK", "WATCH", "2026-10-17T10:15:00.000Z", 1.1],
    ],
  );
});

test("the first loaded reading is not a change (nothing before it)", () => {
  assert.deepEqual(eventsFrom([row(0, "ALERT"), row(15, "ALERT")]), []);
});

test("shock and sensor_error rows are events in their own right", () => {
  const rows = [row(0, "OK"), row(5, "OK", { event: "shock" }), row(10, "OK", { event: "sensor_error", tilt_d: null })];
  const ev = eventsFrom(rows);
  assert.deepEqual(
    ev.map((e) => [e.kind, e.ts]),
    [
      ["sensor_error", "2026-10-17T10:10:00.000Z"],
      ["shock", "2026-10-17T10:05:00.000Z"],
    ],
  );
});

test("a reading that both changes level and is a shock gives both events", () => {
  const ev = eventsFrom([row(0, "OK"), row(5, "ALERT", { event: "shock", tilt_d: 2.5 })]);
  assert.deepEqual(ev.map((e) => e.kind).sort(), ["level", "shock"]);
});

test("rows given newest first (as Supabase returns them) give the same events", () => {
  const rows = [row(0, "OK"), row(15, "WATCH"), row(30, "OK", { event: "shock" })];
  assert.deepEqual(eventsFrom([...rows].reverse()), eventsFrom(rows));
  assert.equal(eventsFrom(rows)[0].kind, "shock");
});
