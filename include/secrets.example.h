#pragma once

// Template for include/secrets.h, which is git-ignored.
// Copy this file to include/secrets.h and fill in real values.

#include <cstddef>

namespace secrets {

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

// Tried in order, each for config::WIFI_CONNECT_TIMEOUT_S.
constexpr WifiNetwork WIFI_NETWORKS[] = {
    {"home-ssid", "home-password"},
    {"school-ssid", "school-password"},
    {"phone-hotspot-ssid", "hotspot-password"},
};
constexpr size_t WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

constexpr const char* SUPABASE_URL = "https://your-project.supabase.co";
constexpr const char* SUPABASE_KEY = "your-device-insert-only-key";

// Hard to guess: anyone who knows the topic can read or post alerts.
constexpr const char* NTFY_TOPIC = "landslide-change-me-to-something-random";

constexpr const char* DEVICE_ID = "node-01";

}  // namespace secrets
