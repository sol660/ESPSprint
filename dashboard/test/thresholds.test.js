// The dashboard's threshold lines must match the firmware's levels
// (include/config.h), so the two can't drift apart.
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";

const repo = new URL("../../", import.meta.url);
const configH = readFileSync(new URL("include/config.h", repo), "utf8");

function firmwareDeg(name) {
  const m = configH.match(new RegExp(`constexpr\\s+float\\s+${name}\\s*=\\s*([0-9.]+)f?\\s*;`));
  assert.ok(m, `${name} not found in include/config.h`);
  return Number(m[1]);
}

async function checkConfig(file) {
  const { CONFIG } = await import(new URL(file, repo).href);
  assert.equal(CONFIG.watchDeg, firmwareDeg("WATCH_DEG"), `${file}: watchDeg`);
  assert.equal(CONFIG.alertDeg, firmwareDeg("ALERT_DEG"), `${file}: alertDeg`);
}

test("config.example.js thresholds match include/config.h", async () => {
  await checkConfig("dashboard/config.example.js");
});

test("config.js thresholds match include/config.h (skipped until it exists)", async (t) => {
  if (!existsSync(new URL("dashboard/config.js", repo))) {
    t.skip("dashboard/config.js not created yet");
    return;
  }
  await checkConfig("dashboard/config.js");
});
