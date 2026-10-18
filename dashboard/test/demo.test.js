// The made-up data behind index.html?demo (for trying the page without
// Supabase) must look like real_readings rows and exercise every part of it.
import { test } from "node:test";
import assert from "node:assert/strict";
import { demoReadings } from "../js/demo.js";
import { eventsFrom, statusOf } from "../js/readings.js";

const cfg = { offlineAfterMinutes: 120 };
const now = new Date("2026-10-17T12:00:00Z");

test("demo rows have the fields the page reads, newest first", () => {
  const rows = demoReadings(now);
  assert.ok(rows.length > 100);
  for (const k of ["ts", "time_synced", "trigger", "pitch_d", "roll_d", "tilt_d", "temp_c", "level", "event"]) {
    assert.ok(k in rows[0], k);
  }
  assert.ok(Date.parse(rows[0].ts) > Date.parse(rows[1].ts));
});

test("demo data is online and has level changes, a shock and a sensor error", () => {
  const rows = demoReadings(now);
  assert.equal(statusOf(rows, now, cfg).offline, false);
  const kinds = new Set(eventsFrom(rows).map((e) => (e.kind === "level" ? e.to : e.kind)));
  for (const k of ["WATCH", "ALERT", "OK", "shock", "sensor_error"]) assert.ok(kinds.has(k), k);
});

test("demo offline variant: newest reading over 2 h old", () => {
  assert.equal(statusOf(demoReadings(now, { offline: true }), now, cfg).offline, true);
});
