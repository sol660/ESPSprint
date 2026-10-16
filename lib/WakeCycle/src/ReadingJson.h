#pragma once

// Reading encoder: one reading as the JSON row of Supabase's readings table
// (supabase/schema.sql; fields from spec #1 plus test_tag from spec #14).
// Hardware-free, so the uplink adapter and the PC tests share it.

#include <cstddef>
#include <cstdint>

#include "Ports.h"

// Per-upload facts that aren't part of the reading itself.
struct UploadHealth {
  const char* deviceId;
  const char* fwVersion;
  const char* resetReason;  // nullptr -> null
  bool hasRssi;
  int rssi;  // dBm of the WiFi link used for the upload
};

// Writes the JSON object into out and returns its length. Returns 0 (and an
// empty string) when out is too small, so a cut-off row is never sent.
// NaN values become null; batt_v is always null (no battery sensing yet).
size_t encodeReadingJson(const Reading& r, const UploadHealth& h, char* out, size_t size);

// A buffer this size (terminating NUL included) holds any row with every number
// at its longest and the device id, fw and reset reason at their usual lengths
// (a full row is about 520 characters). A longer row is refused (0), never cut.
constexpr size_t READING_JSON_BUF_SIZE = 640;

// `YYYY-MM-DDThh:mm:ssZ` for UTC seconds since 1970.
void formatUtcIso8601(uint32_t utc, char* out, size_t size);
