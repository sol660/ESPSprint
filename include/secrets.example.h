#pragma once

// Template for include/secrets.h, which is git-ignored.
// Copy this file to include/secrets.h and fill in real values
// (supabase/SETUP.md says where to find the Supabase ones).
// The board tests on the PC (test/hil) read SUPABASE_URL, SUPABASE_KEY,
// DEVICE_ID and NTFY_TOPIC from include/secrets.h too, so keep each on one line as below.

#include <cstddef>

namespace secrets {

// A WiFi network: WPA2-Personal (a home router: SSID and password) or
// WPA2-Enterprise PEAP (eduroam and most schools: an identity, a username and
// a password). Build entries with personal() or peap() below.
enum class WifiAuth { Personal, EnterprisePeap };

struct WifiNetwork {
  WifiAuth auth;
  const char* ssid;
  const char* password;  // Personal: the WiFi password. PEAP: the account password.
  const char* identity;  // PEAP only: outer identity (often anonymous@your-school); nullptr for Personal.
  const char* username;  // PEAP only: the account login; nullptr for Personal.
};

constexpr WifiNetwork personal(const char* ssid, const char* password) {
  return {WifiAuth::Personal, ssid, password, nullptr, nullptr};
}

constexpr WifiNetwork peap(const char* ssid, const char* identity, const char* username,
                           const char* password) {
  return {WifiAuth::EnterprisePeap, ssid, password, identity, username};
}

// Tried in order (home, then school), each for config::WIFI_CONNECT_TIMEOUT_S.
// Only the first config::WIFI_MAX_NETWORKS are used.
constexpr WifiNetwork WIFI_NETWORKS[] = {
    personal("home-ssid", "home-password"),
    peap("eduroam", "school-identity", "school-username", "school-password"),
};
constexpr size_t WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

// Supabase: Project Settings > API. Use the project URL and the anon /
// publishable key (insert-only through the rules in supabase/schema.sql).
// Never the service_role / secret key: it bypasses those rules.
constexpr const char* SUPABASE_URL = "https://your-project.supabase.co";
constexpr const char* SUPABASE_KEY = "your-anon-or-publishable-key";

// ntfy topic for alerts (ntfy.sh). Hard to guess: anyone who knows it can read or
// post alerts. Subscribe to it in the ntfy app (Android / iPhone).
constexpr const char* NTFY_TOPIC = "landslide-change-me-to-something-random";

constexpr const char* DEVICE_ID = "node-01";

}  // namespace secrets
