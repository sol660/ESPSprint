// Template for dashboard/config.js, which is git-ignored.
// Copy this file to config.js and fill in the two Supabase values
// (dashboard/README.md says where to find them).
//
// Everything in config.js ends up in the published page, so only ever put the
// PUBLIC key here: the anon key (eyJ...) or the publishable key
// (sb_publishable_...). Never the service_role or secret key.

export const CONFIG = {
  // Same values as SUPABASE_URL and SUPABASE_KEY in include/secrets.h.
  supabaseUrl: "https://your-project.supabase.co",
  supabaseKey: "your-anon-or-publishable-key",

  // Same as DEVICE_ID in include/secrets.h.
  deviceId: "node-01",

  // Threshold lines on the tilt chart, degrees. Must equal WATCH_DEG and
  // ALERT_DEG in include/config.h (dashboard/test/thresholds.test.js checks).
  watchDeg: 1.0,
  alertDeg: 2.0,

  // OFFLINE badge when the newest reading is older than this (spec #1: 2 h).
  offlineAfterMinutes: 120,

  // How many of the newest readings to load (Supabase returns at most 1000
  // per request). 1000 is about 10 days of 15-minute readings.
  maxReadings: 1000,

  // Reload the data this often while the page is open.
  refreshMinutes: 5,
};
