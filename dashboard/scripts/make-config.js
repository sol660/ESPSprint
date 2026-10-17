// Builds dashboard/config.js from config.example.js, for the GitHub Pages
// deploy (.github/workflows/dashboard-pages.yml). Run from the repo root:
//
//   SUPABASE_URL=... SUPABASE_KEY=... DEVICE_ID=... node dashboard/scripts/make-config.js
//
// The page is public, so this refuses any key that isn't a public one.

import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

// True for a publishable key or a JWT whose role is anon.
function isPublicKey(key) {
  if (key.startsWith("sb_publishable_")) return true;
  const parts = key.split(".");
  if (parts.length !== 3) return false;
  try {
    const payload = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
    return payload.role === "anon";
  } catch {
    return false;
  }
}

// The template with supabaseUrl, supabaseKey and deviceId replaced.
export function fillConfig(template, values) {
  for (const name of ["supabaseUrl", "supabaseKey", "deviceId"]) {
    if (!values[name]) throw new Error(`${name} is empty`);
  }
  if (!values.supabaseUrl.startsWith("https://")) throw new Error("supabaseUrl must start with https://");
  if (!isPublicKey(values.supabaseKey)) {
    throw new Error("supabaseKey is not a public key: use the anon (eyJ...) or publishable (sb_publishable_...) key, never service_role or secret");
  }
  let out = template;
  for (const [name, value] of Object.entries(values)) {
    const re = new RegExp(`(\\b${name}:\\s*)"[^"\\n]*"`);
    if (!re.test(out)) throw new Error(`${name} not found in the template`);
    // A function replacement, so "$" in a value is taken literally.
    out = out.replace(re, (_, lead) => lead + JSON.stringify(value));
  }
  return out;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const template = readFileSync("dashboard/config.example.js", "utf8");
  const text = fillConfig(template, {
    supabaseUrl: process.env.SUPABASE_URL,
    supabaseKey: process.env.SUPABASE_KEY,
    deviceId: process.env.DEVICE_ID,
  });
  writeFileSync(process.argv[2] || "dashboard/config.js", text);
  console.log("wrote", process.argv[2] || "dashboard/config.js");
}
