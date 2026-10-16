#include "Esp32Ports.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_wpa2.h>
#include <time.h>

#include "AlertMessage.h"
#include "ReadingJson.h"
#include "RootCa.h"
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h is missing: copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

// Placeholders report failure until their ticket implements them.

// ---- Esp32Clock: the chip's clock, kept through deep sleep, set over NTP ----

uint32_t Esp32Clock::nowUtc() { return static_cast<uint32_t>(time(nullptr)); }
// After a power loss the clock restarts at 1970 until the first NTP sync.
bool Esp32Clock::isSynced() { return isUtcValid(nowUtc()); }
uint32_t Esp32Clock::uptimeMs() { return millis(); }

void Esp32Clock::startSync() {  // needs WiFi up
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTime(0, 0, config::NTP_SERVER_1, config::NTP_SERVER_2);  // UTC
  syncStartMs_ = millis();
}

Progress Esp32Clock::pollSync() {
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    log_i("time synced: %lu", static_cast<unsigned long>(nowUtc()));
    return Progress::Done;
  }
  if (millis() - syncStartMs_ < config::NTP_SYNC_TIMEOUT_MS) {
    vTaskDelay(1);  // let other tasks run while a caller spins on this (Clock::sync)
    return Progress::Pending;
  }
  log_w("NTP sync timed out after %lu ms", static_cast<unsigned long>(config::NTP_SYNC_TIMEOUT_MS));
  return Progress::Failed;
}

bool Esp32Store::loadCalibration(TempCalibration&) { return false; }
bool Esp32Store::bufferPush(const Reading&) { return false; }
size_t Esp32Store::bufferCount() { return 0; }
bool Esp32Store::bufferPeek(size_t, Reading&) { return false; }
void Esp32Store::bufferDrop(size_t) {}
// ---- Esp32Uplink: WiFi + Supabase REST ----

namespace {

const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

// One HTTPS POST of body to url, with the server certificate checked against
// the roots in RootCa.h (no setInsecure()) and every wait capped at
// HTTP_TIMEOUT_MS. addHeaders(http) adds the request's headers; isOk(code)
// says which HTTP status counts as accepted. Failures are logged with `what`.
template <typename AddHeaders, typename IsOk>
bool httpsPost(const char* what, const String& url, const uint8_t* body, size_t len,
               AddHeaders addHeaders, IsOk isOk) {
  WiFiClientSecure tls;
  tls.setCACert(root_ca::HTTPS_ROOTS);
  tls.setHandshakeTimeout(config::HTTP_TIMEOUT_MS / 1000);  // seconds; the default is 120
  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  if (!http.begin(tls, url)) {
    log_e("bad URL for %s: %s", what, url.c_str());
    return false;
  }
  addHeaders(http);
  const int code = http.POST(const_cast<uint8_t*>(body), len);
  const bool ok = isOk(code);
  if (!ok) {
    log_e("%s failed: HTTP %d %s", what, code,
          code > 0 ? http.getString().c_str() : http.errorToString(code).c_str());
  }
  http.end();
  return ok;
}

}  // namespace

void Esp32Uplink::startConnect() {
  WiFi.persistent(false);  // don't write the credentials to flash on every wake
  WiFi.mode(WIFI_STA);
  network_ = 0;
  networks_ = min(secrets::WIFI_NETWORK_COUNT, static_cast<size_t>(config::WIFI_MAX_NETWORKS));
  if (networks_ > 0) beginNetwork();
}

void Esp32Uplink::beginNetwork() {
  const secrets::WifiNetwork& net = secrets::WIFI_NETWORKS[network_];
  if (net.auth == secrets::WifiAuth::EnterprisePeap) {
    // No RADIUS server CA is passed (as in the user's eduroam code): see
    // docs/improvements.md #19.
    log_i("WiFi: trying %s (WPA2-Enterprise PEAP)", net.ssid);
    WiFi.begin(net.ssid, WPA2_AUTH_PEAP, net.identity, net.username, net.password);
  } else {
    log_i("WiFi: trying %s", net.ssid);
    // The core leaves Enterprise mode on after a PEAP attempt in this boot.
    esp_wifi_sta_wpa2_ent_disable();
    WiFi.begin(net.ssid, net.password);
  }
  networkStartMs_ = millis();
}

Progress Esp32Uplink::pollConnect() {
  if (network_ >= networks_) return Progress::Failed;
  const secrets::WifiNetwork& net = secrets::WIFI_NETWORKS[network_];
  if (WiFi.status() == WL_CONNECTED) {
    log_i("WiFi: joined %s, RSSI %d dBm", net.ssid, WiFi.RSSI());
    return Progress::Done;
  }
  if (millis() - networkStartMs_ < config::WIFI_CONNECT_TIMEOUT_S * 1000UL) {
    vTaskDelay(1);  // let the WiFi task run while a caller spins on this (Uplink::connect)
    return Progress::Pending;
  }

  log_w("WiFi: %s not joined within %lu s", net.ssid,
        static_cast<unsigned long>(config::WIFI_CONNECT_TIMEOUT_S));
  WiFi.disconnect();
  if (++network_ >= networks_) return Progress::Failed;
  beginNetwork();
  return Progress::Pending;
}

void Esp32Uplink::disconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool Esp32Uplink::sendReadings(const Reading* readings, size_t count) {
  if (WiFi.status() != WL_CONNECTED || count == 0) return false;

  const UploadHealth health{secrets::DEVICE_ID, config::FW_VERSION, resetReasonName(), true,
                            static_cast<int>(WiFi.RSSI())};
  String body;
  body.reserve(count * READING_JSON_BUF_SIZE + 2);  // rows, commas (the NUL's place) and []
  body += '[';
  char row[READING_JSON_BUF_SIZE];
  for (size_t i = 0; i < count; ++i) {
    if (encodeReadingJson(readings[i], health, row, sizeof row) == 0) {
      log_e("reading %lu too long to encode", static_cast<unsigned long>(readings[i].seq));
      return false;
    }
    if (i > 0) body += ',';
    body += row;
  }
  body += ']';

  const String url = String(secrets::SUPABASE_URL) + config::SUPABASE_READINGS_PATH;
  return httpsPost(
      "Supabase insert", url, reinterpret_cast<const uint8_t*>(body.c_str()), body.length(),
      [](HTTPClient& http) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Prefer", "return=minimal");
        http.addHeader("apikey", secrets::SUPABASE_KEY);
        // Legacy anon keys are JWTs and also go in Authorization; new publishable
        // keys (sb_publishable_...) go in apikey only.
        if (strncmp(secrets::SUPABASE_KEY, "eyJ", 3) == 0) {
          http.addHeader("Authorization", String("Bearer ") + secrets::SUPABASE_KEY);
        }
      },
      [](int code) { return code == 201 || code == 200 || code == 204; });
}

// ntfy (spec #1 "Alerts"): one HTTPS POST of the message to the secret topic,
// with the certificate checked against RootCa.h (ISRG Root X1 for ntfy.sh).
bool Esp32Uplink::sendAlert(const Alert& alert) {
  if (WiFi.status() != WL_CONNECTED) return false;
  char body[256];
  char title[80];
  formatAlertMessage(alert, secrets::DEVICE_ID, body, sizeof body);
  formatAlertTitle(alert, secrets::DEVICE_ID, title, sizeof title);

  const String url = String(config::NTFY_SERVER) + "/" + secrets::NTFY_TOPIC;
  return httpsPost(
      "ntfy send", url, reinterpret_cast<const uint8_t*>(body), strlen(body),
      [&](HTTPClient& http) {
        http.addHeader("Content-Type", "text/plain; charset=utf-8");
        http.addHeader("Title", title);
        http.addHeader("Priority", String(alertPriority(alert)));
        http.addHeader("Tags", isTestTag(alert.testTag) ? "test_tube" : "warning");
      },
      [](int code) { return code == 200; });
}

void Esp32CamTrigger::pulse() { log_w("CAM pulse requested (not wired yet)"); }

void Esp32Sleeper::sleep(uint32_t timerS, bool motionWake) {
  if (motionWake) log_w("motion wake requested but not wired yet (#5): timer only");
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timerS) * 1000000ULL);
  esp_deep_sleep_start();  // never returns: the next wake boots from setup()
}

WakeCause readWakeCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WakeCause::Timer;
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
      return WakeCause::Motion;
    default:
      return WakeCause::PowerOn;
  }
}

// ---- Esp32Imu: MPU6050 over I2C (registers from the MPU-6000/6050 register map) ----

namespace {

constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;  // 14 bytes: accel xyz, temp, gyro xyz
constexpr uint8_t REG_TEMP_OUT_H = 0x41;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr float ACCEL_LSB_PER_G = 16384.0f;  // +/-2 g
constexpr float GYRO_LSB_PER_DPS = 131.0f;   // +/-250 deg/s

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(config::MPU_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(config::MPU_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(config::MPU_I2C_ADDR, static_cast<uint8_t>(len)) != len) return false;
  for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

int16_t be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }

}  // namespace

bool Esp32Imu::begin() {
  Wire.begin(config::PIN_I2C_SDA, config::PIN_I2C_SCL, config::I2C_CLOCK_HZ);
  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) {
    log_e("MPU6050 not answering at 0x%02x", config::MPU_I2C_ADDR);
    return false;
  }
  if (who != 0x68) log_w("unexpected WHO_AM_I 0x%02x (clone?), continuing", who);
  // With the DLPF on, the internal rate is 1 kHz: rate = 1000 / (1 + div).
  const uint8_t div = static_cast<uint8_t>(1000 / config::IMU_SAMPLE_RATE_HZ - 1);
  return writeReg(REG_PWR_MGMT_1, 0x01) &&    // wake, clock from gyro X PLL
         writeReg(REG_CONFIG, 0x06) &&        // DLPF lowest setting (5 Hz)
         writeReg(REG_SMPLRT_DIV, div) &&
         writeReg(REG_GYRO_CONFIG, 0x00) &&   // +/-250 deg/s
         writeReg(REG_ACCEL_CONFIG, 0x00);    // +/-2 g
}

bool Esp32Imu::readSample(ImuSample& out) {
  // Pace to the sensor's output rate so consecutive samples aren't duplicates.
  const uint32_t periodUs = 1000000UL / config::IMU_SAMPLE_RATE_HZ;
  while (micros() - lastSampleUs_ < periodUs) {
  }
  lastSampleUs_ = micros();

  uint8_t b[14];
  if (!readRegs(REG_ACCEL_XOUT_H, b, sizeof b)) return false;
  out.ax = be16(b + 0) / ACCEL_LSB_PER_G;
  out.ay = be16(b + 2) / ACCEL_LSB_PER_G;
  out.az = be16(b + 4) / ACCEL_LSB_PER_G;
  out.gx = be16(b + 8) / GYRO_LSB_PER_DPS;
  out.gy = be16(b + 10) / GYRO_LSB_PER_DPS;
  out.gz = be16(b + 12) / GYRO_LSB_PER_DPS;
  return true;
}

bool Esp32Imu::readTemperatureC(float& out) {
  uint8_t b[2];
  if (!readRegs(REG_TEMP_OUT_H, b, sizeof b)) return false;
  out = be16(b) / 340.0f + 36.53f;
  return true;
}

bool Esp32Imu::setMotionThreshold(float) { return false; }  // wake-on-motion: #5

bool Esp32Imu::resetBus() {  // full bus recovery: #7
  Wire.end();
  return begin();
}

// ---- Esp32Store: flash via Preferences (NVS) ----

namespace {
constexpr const char* KEY_BASELINE = "baseline";
constexpr const char* KEY_BOOT_COUNT = "boots";
constexpr const char* KEY_ALERT_QUEUE = "alerts";  // Alerts not yet delivered to ntfy, oldest first
constexpr const char* KEY_OLD_PENDING_ALERT = "alert";  // the first build's single pending Alert

// Opens the namespace, runs fn(prefs) and closes it again. False if the
// namespace can't be opened (read-only opening fails if it was never written).
template <typename Fn>
bool withPrefs(const char* ns, bool readOnly, Fn fn) {
  Preferences prefs;
  if (!prefs.begin(ns, readOnly)) return false;
  const bool ok = fn(prefs);
  prefs.end();
  return ok;
}

// A fixed-size blob. isKey first: getBytesLength logs an error for a missing key.
bool getBlob(Preferences& prefs, const char* key, void* out, size_t size) {
  return prefs.isKey(key) && prefs.getBytesLength(key) == size &&
         prefs.getBytes(key, out, size) == size;
}

bool putBlob(Preferences& prefs, const char* key, const void* data, size_t size) {
  return prefs.putBytes(key, data, size) == size;
}

// The queue lives as one blob of up to ALERT_QUEUE_LEN Alerts.
struct AlertQueue {
  Alert items[config::ALERT_QUEUE_LEN];
  size_t count = 0;
};

bool readAlertQueue(Preferences& prefs, AlertQueue& q) {
  q.count = 0;
  if (prefs.isKey(KEY_ALERT_QUEUE)) {
    const size_t len = prefs.getBytesLength(KEY_ALERT_QUEUE);
    if (len % sizeof(Alert) == 0 && len <= sizeof q.items &&
        prefs.getBytes(KEY_ALERT_QUEUE, q.items, len) == len) {
      q.count = len / sizeof(Alert);
    } else {
      log_w("alert queue in flash unreadable (%u bytes): starting it again", static_cast<unsigned>(len));
    }
  }
  // One pending alert left by the first build joins the queue.
  if (q.count == 0 && getBlob(prefs, KEY_OLD_PENDING_ALERT, &q.items[0], sizeof(Alert))) q.count = 1;
  return true;
}

bool writeAlertQueue(Preferences& prefs, const AlertQueue& q) {
  if (prefs.isKey(KEY_OLD_PENDING_ALERT)) prefs.remove(KEY_OLD_PENDING_ALERT);
  if (q.count == 0) return !prefs.isKey(KEY_ALERT_QUEUE) || prefs.remove(KEY_ALERT_QUEUE);
  return putBlob(prefs, KEY_ALERT_QUEUE, q.items, q.count * sizeof(Alert));
}

bool loadAlertQueue(const char* ns, AlertQueue& q) {
  return withPrefs(ns, true, [&](Preferences& p) { return readAlertQueue(p, q); });
}

// Reads the queue, lets edit change it and writes it back.
template <typename Edit>
bool editAlertQueue(const char* ns, Edit edit) {
  return withPrefs(ns, false, [&](Preferences& p) {
    AlertQueue q;
    return readAlertQueue(p, q) && edit(q) && writeAlertQueue(p, q);
  });
}

}  // namespace

size_t Esp32Store::alertCount() {
  AlertQueue q;
  return loadAlertQueue(ns_, q) ? q.count : 0;
}

bool Esp32Store::alertPeek(size_t index, Alert& out) {
  AlertQueue q;
  if (!loadAlertQueue(ns_, q)) return false;
  if (index >= q.count) return false;
  out = q.items[index];
  return true;
}

bool Esp32Store::alertPush(const Alert& a) {
  const bool ok = editAlertQueue(ns_, [&](AlertQueue& q) {
    if (q.count >= config::ALERT_QUEUE_LEN) return false;
    q.items[q.count++] = a;
    return true;
  });
  if (!ok) log_e("alert save to flash failed: it is lost if this wake can't send it");
  return ok;
}

bool Esp32Store::alertReplaceNewest(const Alert& a) {
  const bool ok = editAlertQueue(ns_, [&](AlertQueue& q) {
    if (q.count == 0) return false;
    q.items[q.count - 1] = a;
    return true;
  });
  if (!ok) log_e("alert update in flash failed");
  return ok;
}

bool Esp32Store::alertDrop(size_t count) {
  const bool ok = editAlertQueue(ns_, [&](AlertQueue& q) {
    const size_t n = count < q.count ? count : q.count;
    for (size_t i = n; i < q.count; ++i) q.items[i - n] = q.items[i];
    q.count -= n;
    return true;
  });
  if (!ok) log_e("alert clear in flash failed: cleared on a later wake instead");
  return ok;
}

bool Esp32Store::loadBootCount(uint32_t& out) {
  return withPrefs(ns_, true, [&](Preferences& p) {
    if (!p.isKey(KEY_BOOT_COUNT)) return false;
    out = p.getULong(KEY_BOOT_COUNT, 0);
    return true;
  });
}

bool Esp32Store::saveBootCount(uint32_t count) {
  const bool ok = withPrefs(ns_, false, [&](Preferences& p) {
    return p.putULong(KEY_BOOT_COUNT, count) == sizeof(uint32_t);
  });
  if (!ok) log_e("boot count save to flash failed: boot_count may repeat after a power loss");
  return ok;
}

bool Esp32Store::loadBaseline(Baseline& out) {
  return withPrefs(ns_, true,
                   [&](Preferences& p) { return getBlob(p, KEY_BASELINE, &out, sizeof out); });
}

bool Esp32Store::saveBaseline(const Baseline& b) {
  const bool ok =
      withPrefs(ns_, false, [&](Preferences& p) { return putBlob(p, KEY_BASELINE, &b, sizeof b); });
  if (!ok) log_e("baseline save to flash failed");
  return ok;
}
