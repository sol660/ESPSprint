// Status card and OFFLINE badge (issue #9).
import { test } from "node:test";
import assert from "node:assert/strict";
import { statusOf } from "../js/readings.js";

const cfg = { offlineAfterMinutes: 120 };

function row(ts, extra = {}) {
  return { ts, level: "OK", event: "none", tilt_d: 0.1, temp_c: 20, ...extra };
}

test("newest reading 1 h old: online, card shows that reading", () => {
  const rows = [row("2026-10-17T09:00:00Z"), row("2026-10-17T11:00:00Z", { level: "WATCH", tilt_d: 1.2 })];
  const s = statusOf(rows, new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.offline, false);
  assert.equal(s.latest.level, "WATCH");
  assert.equal(s.latest.tilt_d, 1.2);
  assert.equal(s.ageMinutes, 60);
});

test("newest reading just over 2 h old: OFFLINE", () => {
  const s = statusOf([row("2026-10-17T09:59:00Z")], new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.offline, true);
});

test("newest reading exactly 2 h old: not yet OFFLINE (more than 2 h)", () => {
  const s = statusOf([row("2026-10-17T10:00:00Z")], new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.offline, false);
});

test("rows in any order: the newest by ts is the latest", () => {
  const rows = [row("2026-10-17T11:30:00Z", { level: "ALERT" }), row("2026-10-17T08:00:00Z")];
  const s = statusOf(rows, new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.latest.level, "ALERT");
  assert.equal(s.offline, false);
});

test("no readings at all: OFFLINE with no latest reading", () => {
  const s = statusOf([], new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.offline, true);
  assert.equal(s.latest, null);
});

test("a reading with an unset clock (ts in 1970) is ignored", () => {
  // time_synced=false before the first sync: ts counts from power-on.
  const rows = [row("2026-10-17T07:00:00Z"), row("1970-01-01T00:05:00Z", { time_synced: false, level: "ALERT" })];
  const s = statusOf(rows, new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.latest.ts, "2026-10-17T07:00:00Z");
  assert.equal(s.offline, true);
});

test("Supabase timestamps with +00:00 offsets and fractions parse", () => {
  const s = statusOf([row("2026-10-17T11:00:00.5+00:00")], new Date("2026-10-17T12:00:00Z"), cfg);
  assert.equal(s.offline, false);
  assert.ok(s.latest);
});
