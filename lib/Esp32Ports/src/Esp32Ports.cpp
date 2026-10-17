#include "Esp32Ports.h"

#include <HTTPClient.h>
#include <LittleFS.h>
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

// ---- Esp32Store offline buffer (#7): a ring of Readings in a LittleFS file ----
//
// NVS (~20 KB) can't hold config::BUFFER_CAPACITY readings, so the buffer
// lives on LittleFS in the default partition table's data partition (label
// "spiffs", ~1.4 MB). Two files per namespace:
//  - /<ns>.buf: up to BUFFER_CAPACITY fixed-size records (raw Reading structs),
//    written in ring order; it only grows up to its full size;
//  - /<ns>.bhd: BufferHeader (where the oldest record is and how many there are).
// A push writes its record, then the header; a drop writes only the header.
// LittleFS commits a file only on close, so a power loss leaves the old header
// (at worst the reading being pushed is lost, never the buffer).

namespace {

constexpr uint32_t BUFFER_MAGIC = 0x46554245;  // "EBUF"
// Bump when Reading's layout changes: an old buffer is then started again.
constexpr uint16_t BUFFER_LAYOUT_VERSION = 2;  // 2: Reading::uploadRefusals

struct BufferHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t capacity;
  uint32_t head;   // slot of the oldest reading
  uint32_t count;  // readings in the buffer
};

bool mountFs() {
  static bool mounted = false;
  if (!mounted) {
    mounted = LittleFS.begin(true);  // formats the partition the first time
    if (!mounted) log_e("LittleFS mount failed: no offline buffer this wake");
  }
  return mounted;
}

String bufferPath(const char* ns, const char* ext) { return String("/") + ns + ext; }

BufferHeader emptyHeader() {
  return {BUFFER_MAGIC, BUFFER_LAYOUT_VERSION, static_cast<uint16_t>(sizeof(Reading)),
          static_cast<uint32_t>(config::BUFFER_CAPACITY), 0, 0};
}

// The header, or an empty one when there is none (or it is from another layout).
bool readBufferHeader(const char* ns, BufferHeader& h) {
  h = emptyHeader();
  if (!mountFs()) return false;
  const String path = bufferPath(ns, ".bhd");
  if (!LittleFS.exists(path)) return true;
  File f = LittleFS.open(path, "r");
  BufferHeader stored{};
  const bool read = f && f.read(reinterpret_cast<uint8_t*>(&stored), sizeof stored) == sizeof stored;
  if (f) f.close();
  if (read && stored.magic == BUFFER_MAGIC && stored.version == BUFFER_LAYOUT_VERSION &&
      stored.recordSize == sizeof(Reading) && stored.capacity == config::BUFFER_CAPACITY &&
      stored.head < stored.capacity && stored.count <= stored.capacity) {
    h = stored;
  } else {
    log_w("offline buffer header unreadable or from another firmware: starting it again");
  }
  return true;
}

bool writeBufferHeader(const char* ns, const BufferHeader& h) {
  File f = LittleFS.open(bufferPath(ns, ".bhd"), "w");
  if (!f) return false;
  const bool ok = f.write(reinterpret_cast<const uint8_t*>(&h), sizeof h) == sizeof h;
  f.close();
  return ok;
}

}  // namespace

bool Esp32Store::bufferPush(const Reading& r) {
  BufferHeader h;
  bool ok = readBufferHeader(ns_, h);
  if (ok) {
    const uint32_t slot = (h.head + h.count) % h.capacity;
    const String path = bufferPath(ns_, ".buf");
    const size_t offset = static_cast<size_t>(slot) * sizeof(Reading);
    const size_t fileSize = LittleFS.exists(path) ? LittleFS.open(path, "r").size() : 0;
    // The ring only grows into the file at its end, so the slot is either inside
    // the file (overwrite) or exactly at its end (append).
    File f = offset < fileSize ? LittleFS.open(path, "r+") : LittleFS.open(path, "a");
    ok = f && (offset >= fileSize || f.seek(offset)) && (offset <= fileSize) &&
         f.write(reinterpret_cast<const uint8_t*>(&r), sizeof r) == sizeof r;
    if (f) f.close();
    if (ok) {
      if (h.count < h.capacity) {
        ++h.count;
      } else {
        h.head = (h.head + 1) % h.capacity;  // full: the oldest is overwritten
      }
      ok = writeBufferHeader(ns_, h);
    }
  }
  if (!ok) log_e("offline buffer save failed: reading %lu lost", static_cast<unsigned long>(r.seq));
  return ok;
}

size_t Esp32Store::bufferCount() {
  BufferHeader h;
  return readBufferHeader(ns_, h) ? h.count : 0;
}

bool Esp32Store::bufferPeek(size_t index, Reading& out) {
  BufferHeader h;
  if (!readBufferHeader(ns_, h) || index >= h.count) return false;
  const size_t slot = (h.head + index) % h.capacity;
  File f = LittleFS.open(bufferPath(ns_, ".buf"), "r");
  const bool ok = f && f.seek(slot * sizeof(Reading)) &&
                  f.read(reinterpret_cast<uint8_t*>(&out), sizeof out) == sizeof out;
  if (f) f.close();
  if (!ok) log_e("offline buffer read failed at %u", static_cast<unsigned>(index));
  return ok;
}

// Rewrites the oldest record in place (its refusal count); the header is unchanged.
bool Esp32Store::bufferReplaceOldest(const Reading& r) {
  BufferHeader h;
  bool ok = readBufferHeader(ns_, h) && h.count > 0;
  if (ok) {
    File f = LittleFS.open(bufferPath(ns_, ".buf"), "r+");
    ok = f && f.seek(static_cast<size_t>(h.head) * sizeof(Reading)) &&
         f.write(reinterpret_cast<const uint8_t*>(&r), sizeof r) == sizeof r;
    if (f) f.close();
  }
  if (!ok) log_e("offline buffer: refusal count of reading %lu not saved", static_cast<unsigned long>(r.seq));
  return ok;
}

void Esp32Store::bufferDrop(size_t count) {
  BufferHeader h;
  if (!readBufferHeader(ns_, h) || count == 0 || h.count == 0) return;
  const uint32_t n = count < h.count ? static_cast<uint32_t>(count) : h.count;
  h.head = (h.head + n) % h.capacity;
  h.count -= n;
  // Empty: start at slot 0 again. The file keeps its size; slot 0 is inside it.
  if (h.count == 0) h.head = 0;
  if (!writeBufferHeader(ns_, h)) log_e("offline buffer clear failed: those readings are sent again");
}
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
  lastRefused_ = false;
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
      lastRefused_ = true;  // that reading can never go up: treated like a refused row (#7)
      return false;
    }
    if (i > 0) body += ',';
    body += row;
  }
  body += ']';

  // A reading the server already has (a re-sent buffered one) is skipped, not
  // an error, so the rest of the post still goes in (#7).
  const String url = String(secrets::SUPABASE_URL) + config::SUPABASE_READINGS_PATH +
                     config::SUPABASE_READINGS_UPSERT_QUERY;
  return httpsPost(
      "Supabase insert", url, reinterpret_cast<const uint8_t*>(body.c_str()), body.length(),
      [](HTTPClient& http) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Prefer", "return=minimal,resolution=ignore-duplicates");
        http.addHeader("apikey", secrets::SUPABASE_KEY);
        // Only rows carrying this device's token are accepted (#24): the public
        // key alone, which the dashboard shows, can't insert.
        http.addHeader(config::SUPABASE_DEVICE_TOKEN_HEADER, secrets::DEVICE_TOKEN);
        // Legacy anon keys are JWTs and also go in Authorization; new publishable
        // keys (sb_publishable_...) go in apikey only.
        if (strncmp(secrets::SUPABASE_KEY, "eyJ", 3) == 0) {
          http.addHeader("Authorization", String("Bearer ") + secrets::SUPABASE_KEY);
        }
      },
      [this](int code) {
        // The server refused the rows themselves (bad JSON, a check or not-null
        // constraint, too large): the core then looks for the bad one (#7).
        lastRefused_ = code == 400 || code == 409 || code == 413 || code == 422;
        return code == 201 || code == 200 || code == 204;
      });
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

// I2C bus recovery (#7): a reset ESP32 or a glitch can leave the MPU6050 halfway
// through a byte, holding SDA low. Up to 9 SCL clocks let it finish, then a STOP
// frees the bus, and the sensor is set up again. A few hundred microseconds.
bool Esp32Imu::resetBus() {
  log_w("MPU6050 not answering: resetting the I2C bus");
  Wire.end();
  const uint8_t sda = config::PIN_I2C_SDA;
  const uint8_t scl = config::PIN_I2C_SCL;
  constexpr uint32_t HALF_CLOCK_US = 5;  // 100 kHz
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  digitalWrite(scl, HIGH);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; ++i) {
    digitalWrite(scl, LOW);
    delayMicroseconds(HALF_CLOCK_US);
    digitalWrite(scl, HIGH);
    delayMicroseconds(HALF_CLOCK_US);
  }
  // STOP: SDA low to high while SCL is high.
  pinMode(sda, OUTPUT_OPEN_DRAIN);
  digitalWrite(sda, LOW);
  delayMicroseconds(HALF_CLOCK_US);
  digitalWrite(scl, HIGH);
  delayMicroseconds(HALF_CLOCK_US);
  digitalWrite(sda, HIGH);
  delayMicroseconds(HALF_CLOCK_US);
  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
  const bool ok = begin();
  if (!ok) log_e("MPU6050 still not answering after the I2C bus reset");
  return ok;
}

// ---- Esp32Store: flash via Preferences (NVS) ----

namespace {
constexpr const char* KEY_BASELINE = "baseline";
constexpr const char* KEY_ACCEL_CAL = "accelcal";  // AccelCalibration from `cal save` (#22)
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

// Removes `key` from the namespace. True when it is gone afterwards, also when
// it was never there (a namespace that was never written can't open read-write
// only if flash is faulty).
static bool removeKey(const char* ns, const char* key) {
  return withPrefs(ns, false, [&](Preferences& p) { return !p.isKey(key) || p.remove(key); });
}

bool Esp32Store::clearBaseline() {
  const bool ok = removeKey(ns_, KEY_BASELINE);
  if (!ok) log_e("baseline erase from flash failed");
  return ok;
}

bool Esp32Store::loadAccelCalibration(AccelCalibration& out) {
  return withPrefs(ns_, true,
                   [&](Preferences& p) { return getBlob(p, KEY_ACCEL_CAL, &out, sizeof out); });
}

bool Esp32Store::saveAccelCalibration(const AccelCalibration& c) {
  const bool ok =
      withPrefs(ns_, false, [&](Preferences& p) { return putBlob(p, KEY_ACCEL_CAL, &c, sizeof c); });
  if (!ok) log_e("accelerometer calibration save to flash failed");
  return ok;
}

bool Esp32Store::clearAccelCalibration() {
  const bool ok = removeKey(ns_, KEY_ACCEL_CAL);
  if (!ok) log_e("accelerometer calibration erase from flash failed");
  return ok;
}
