#pragma once

// Template for include/secrets.h, which is git-ignored.
// Copy this file to include/secrets.h and fill in real values
// (supabase/SETUP.md says where to find the Supabase ones).
// The board tests on the PC (test/hil) read SUPABASE_URL, SUPABASE_KEY and
// DEVICE_ID from include/secrets.h too, so keep each on one line as below.

#include <cstddef>

namespace secrets {

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

// Tried in order (home, then school), each for config::WIFI_CONNECT_TIMEOUT_S.
// Only the first config::WIFI_MAX_NETWORKS are used.
constexpr WifiNetwork WIFI_NETWORKS[] = {
    {"home-ssid", "home-password"},
    {"school-ssid", "school-password"},
};
constexpr size_t WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

// Supabase: Project Settings > API. Use the project URL and the anon /
// publishable key (insert-only through the rules in supabase/schema.sql).
// Never the service_role / secret key: it bypasses those rules.
constexpr const char* SUPABASE_URL = "https://your-project.supabase.co";
constexpr const char* SUPABASE_KEY = "your-anon-or-publishable-key";

// Hard to guess: anyone who knows the topic can read or post alerts.
constexpr const char* NTFY_TOPIC = "landslide-change-me-to-something-random";

constexpr const char* DEVICE_ID = "node-01";

}  // namespace secrets
