// Chart data shaping (issue #9): tilt delta (pitch, roll, combined) with the
// WATCH and ALERT lines, and temperature. Points are { x: ms since 1970, y }.
import { test } from "node:test";
import assert from "node:assert/strict";
import { chartData } from "../js/readings.js";

const cfg = { watchDeg: 1.0, alertDeg: 2.0 };
const T0 = Date.UTC(2026, 9, 17, 10, 0);
const T1 = Date.UTC(2026, 9, 17, 10, 15);

const rows = [
  // newest first, as Supabase returns them
  { ts: "2026-10-17T10:15:00Z", pitch_d: -0.5, roll_d: 0.25, tilt_d: 0.56, temp_c: 21.5 },
  { ts: "2026-10-17T10:00:00Z", pitch_d: 0.1, roll_d: 0.2, tilt_d: 0.22, temp_c: 21.0 },
];

test("tilt series are oldest first, one point per reading", () => {
  const d = chartData(rows, cfg);
  assert.deepEqual(d.pitch, [{ x: T0, y: 0.1 }, { x: T1, y: -0.5 }]);
  assert.deepEqual(d.roll, [{ x: T0, y: 0.2 }, { x: T1, y: 0.25 }]);
  assert.deepEqual(d.tilt, [{ x: T0, y: 0.22 }, { x: T1, y: 0.56 }]);
});

test("temperature series", () => {
  assert.deepEqual(chartData(rows, cfg).temp, [{ x: T0, y: 21.0 }, { x: T1, y: 21.5 }]);
});

test("WATCH and ALERT lines run flat across the whole time range at the configured values", () => {
  const d = chartData(rows, cfg);
  assert.deepEqual(d.watch, [{ x: T0, y: 1.0 }, { x: T1, y: 1.0 }]);
  assert.deepEqual(d.alert, [{ x: T0, y: 2.0 }, { x: T1, y: 2.0 }]);
});

test("a sensor-error reading (null angles) leaves a gap, not a zero", () => {
  const withError = [
    { ts: "2026-10-17T10:30:00Z", pitch_d: null, roll_d: null, tilt_d: null, temp_c: null },
    ...rows,
  ];
  const d = chartData(withError, cfg);
  assert.equal(d.tilt.length, 3);
  assert.equal(d.tilt[2].y, null);
  assert.equal(d.temp[2].y, null);
});

test("no readings: empty series and no threshold lines", () => {
  const d = chartData([], cfg);
  assert.deepEqual([d.pitch, d.roll, d.tilt, d.temp, d.watch, d.alert], [[], [], [], [], [], []]);
});
