// The Pages deploy builds config.js from config.example.js and three values.
// It must refuse anything but a public key, since the page is public.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fillConfig } from "../scripts/make-config.js";

const template = readFileSync(new URL("../config.example.js", import.meta.url), "utf8");

// A JWT with the given payload (signature isn't checked).
const jwt = (payload) =>
  ["e30", Buffer.from(JSON.stringify(payload)).toString("base64url"), "sig"].join(".");

const good = {
  supabaseUrl: "https://abcdefgh.supabase.co",
  supabaseKey: "sb_publishable_AbC123",
  deviceId: "node-07",
};

async function load(text) {
  const { CONFIG } = await import(`data:text/javascript,${encodeURIComponent(text)}`);
  return CONFIG;
}

test("fills in the URL, key and device id and keeps the rest of the template", async () => {
  const cfg = await load(fillConfig(template, good));
  assert.equal(cfg.supabaseUrl, "https://abcdefgh.supabase.co");
  assert.equal(cfg.supabaseKey, "sb_publishable_AbC123");
  assert.equal(cfg.deviceId, "node-07");
  assert.equal(cfg.watchDeg, 1.0);
  assert.equal(cfg.offlineAfterMinutes, 120);
});

test("accepts a legacy anon JWT", async () => {
  const key = jwt({ iss: "supabase", role: "anon" });
  assert.equal((await load(fillConfig(template, { ...good, supabaseKey: key }))).supabaseKey, key);
});

test("refuses a secret key", () => {
  assert.throws(() => fillConfig(template, { ...good, supabaseKey: "sb_secret_xyz" }), /public/);
});

test("refuses a service_role JWT", () => {
  const key = jwt({ iss: "supabase", role: "service_role" });
  assert.throws(() => fillConfig(template, { ...good, supabaseKey: key }), /public/);
});

test("refuses a missing value", () => {
  assert.throws(() => fillConfig(template, { ...good, deviceId: "" }), /deviceId/);
});

test("refuses a URL that isn't https", () => {
  assert.throws(() => fillConfig(template, { ...good, supabaseUrl: "http://x.supabase.co" }), /https/);
});

test("quotes and backslashes in a value can't break out of the string", async () => {
  const cfg = await load(fillConfig(template, { ...good, deviceId: 'a"b\\c' }));
  assert.equal(cfg.deviceId, 'a"b\\c');
});
