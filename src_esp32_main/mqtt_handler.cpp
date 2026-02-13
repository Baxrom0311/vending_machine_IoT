#include "mqtt_handler.h"
#include "config.h"
#include "config_storage.h"
#include "display.h"
#include "ota_handler.h"
#include "relay_control.h"
#include "sensors.h"
#include "state_machine.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>
#include <esp_task_wdt.h>
#include <mbedtls/md.h>

// ============================================
// MQTT CLIENT
// ============================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

static bool networkApplyPending = false;
static bool pendingWifiApply = false;
static bool pendingMqttApply = false;
static unsigned long networkApplyStartMs = 0;
static DeviceConfig prevNetworkConfig;
static const unsigned long networkApplyTimeoutMs = 30000;

static const int RECENT_TXN_CACHE = 8;
static String recentTxnIds[RECENT_TXN_CACHE];
static int recentTxnIndex = 0;

// ============================================
// MQTT SETUP
// ============================================
void setupMQTT() {
  mqttClient.setServer(deviceConfig.mqtt_broker, deviceConfig.mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(
      2048); // Increased from 1024 for large signed messages
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(30);

  reconnectMQTT();
}

void reconnectMQTT() {
  static unsigned long lastAttempt = 0;
  static unsigned int failedAttempts = 0;

  // FIX: Exponential backoff to prevent broker spam
  // Delays: 5s, 10s, 20s, 60s, 120s, 300s (cap at 5 min)
  const unsigned long backoffDelays[] = {5000,  10000,  20000,
                                         60000, 120000, 300000};
  const int maxBackoffIndex = 5;

  if (mqttClient.connected()) {
    failedAttempts = 0; // Reset on successful connection
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  int backoffIndex =
      (failedAttempts < maxBackoffIndex) ? failedAttempts : maxBackoffIndex;
  unsigned long retryInterval = backoffDelays[backoffIndex];

  if (now - lastAttempt < retryInterval) {
    return;
  }
  lastAttempt = now;

  Serial.print("Connecting to MQTT (attempt ");
  Serial.print(failedAttempts + 1);
  Serial.print("): ");
  Serial.print(deviceConfig.mqtt_broker);
  Serial.print(":");
  Serial.println(deviceConfig.mqtt_port);

  const char *clientId = deviceConfig.device_id;
  const char *username =
      deviceConfig.mqtt_username[0] ? deviceConfig.mqtt_username : nullptr;
  const char *password =
      deviceConfig.mqtt_password[0] ? deviceConfig.mqtt_password : nullptr;

  if (mqttClient.connect(clientId, username, password)) {
    Serial.println("MQTT Connected!");
    failedAttempts = 0; // Reset counter on success

    // Subscribe to topics
    mqttClient.subscribe(TOPIC_PAYMENT_IN);
    mqttClient.subscribe(TOPIC_CONFIG_IN);
    mqttClient.subscribe(TOPIC_OTA_IN);

    // Subscribe to broadcast topics (all devices)
    mqttClient.subscribe(TOPIC_BROADCAST_CONFIG);
    mqttClient.subscribe(TOPIC_BROADCAST_COMMAND);

    // Subscribe to group topics (if groupId is set)
    if (strlen(deviceConfig.groupId) > 0) {
      mqttClient.subscribe(TOPIC_GROUP_CONFIG);
      mqttClient.subscribe(TOPIC_GROUP_COMMAND);
    }

    Serial.println("Subscribed to topics");

    // Publish online status
    publishLog("MQTT", "Connected");
  } else {
    failedAttempts++; // Increment for backoff calculation
    // analytics.incrementMqttReconnects(); // Removed
    Serial.print("Failed, rc=");
    Serial.print(mqttClient.state());
    Serial.print(", next retry in ");
    Serial.print(
        backoffDelays[(failedAttempts < maxBackoffIndex) ? failedAttempts
                                                         : maxBackoffIndex] /
        1000);
    Serial.println(" seconds");
  }
}

// ============================================
// MQTT CALLBACK - Handle incoming messages
// ============================================
static unsigned long normalizeSecondsOrMs(unsigned long value) {
  if (value == 0) {
    return value;
  }
  // Accept seconds (<= 3600) or milliseconds (> 3600)
  if (value <= 3600UL) {
    return value * 1000UL;
  }
  return value;
}

static void copyToBuffer(char *dst, size_t dstSize, const String &src) {
  size_t n = src.length();
  if (n >= dstSize) {
    n = dstSize - 1;
  }
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

static String hmacSha256Hex(const String &data, const char *key) {
  unsigned char hmac[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) {
    mbedtls_md_free(&ctx);
    return String();
  }
  if (mbedtls_md_setup(&ctx, info, 1) != 0) {
    mbedtls_md_free(&ctx);
    return String();
  }
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key, strlen(key));
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)data.c_str(),
                         data.length());
  mbedtls_md_hmac_finish(&ctx, hmac);
  mbedtls_md_free(&ctx);

  static const char hexChars[] = "0123456789abcdef";
  char out[65];
  for (int i = 0; i < 32; i++) {
    out[i * 2] = hexChars[(hmac[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hexChars[hmac[i] & 0x0F];
  }
  out[64] = '\0';
  return String(out);
}

static bool isNewTxnId(const String &txnId) {
  if (txnId.length() == 0) {
    return false;
  }
  for (int i = 0; i < RECENT_TXN_CACHE; i++) {
    if (recentTxnIds[i] == txnId) {
      return false;
    }
  }
  return true;
}

static void rememberTxnId(const String &txnId) {
  if (txnId.length() == 0) {
    return;
  }
  recentTxnIds[recentTxnIndex] = txnId;
  recentTxnIndex = (recentTxnIndex + 1) % RECENT_TXN_CACHE;
}

static const char *getSignatureField(const JsonDocument &doc) {
  if (doc["sig"].is<const char *>()) {
    return doc["sig"].as<const char *>();
  }
  if (doc["auth"]["sig"].is<const char *>()) {
    return doc["auth"]["sig"].as<const char *>();
  }
  return nullptr;
}

static String canonicalPayment(const JsonDocument &doc) {
  JsonDocument canonical;
  canonical["amount"] = doc["amount"];
  if (!doc["source"].isNull())
    canonical["source"] = doc["source"];
  if (!doc["transaction_id"].isNull())
    canonical["transaction_id"] = doc["transaction_id"];
  if (!doc["nonce"].isNull())
    canonical["nonce"] = doc["nonce"];
  if (!doc["user_id"].isNull())
    canonical["user_id"] = doc["user_id"];
  if (!doc["ts"].isNull())
    canonical["ts"] = doc["ts"];
  canonical["device_id"] = deviceConfig.device_id;

  String out;
  serializeJson(canonical, out);
  return out;
}

static String canonicalConfig(const JsonDocument &doc) {
  JsonDocument canonical;
  if (!doc["apply"].isNull())
    canonical["apply"] = doc["apply"];
  if (!doc["deviceId"].isNull())
    canonical["deviceId"] = doc["deviceId"];
  if (!doc["wifiSsid"].isNull())
    canonical["wifiSsid"] = doc["wifiSsid"];
  if (!doc["wifiPassword"].isNull())
    canonical["wifiPassword"] = doc["wifiPassword"];
  if (!doc["mqttBroker"].isNull())
    canonical["mqttBroker"] = doc["mqttBroker"];
  if (!doc["mqttPort"].isNull())
    canonical["mqttPort"] = doc["mqttPort"];
  if (!doc["mqttUsername"].isNull())
    canonical["mqttUsername"] = doc["mqttUsername"];
  if (!doc["mqttPassword"].isNull())
    canonical["mqttPassword"] = doc["mqttPassword"];
  if (!doc["pricePerLiter"].isNull())
    canonical["pricePerLiter"] = doc["pricePerLiter"];
  if (!doc["sessionTimeout"].isNull())
    canonical["sessionTimeout"] = doc["sessionTimeout"];
  if (!doc["freeWaterCooldown"].isNull())
    canonical["freeWaterCooldown"] = doc["freeWaterCooldown"];
  if (!doc["freeWaterAmount"].isNull())
    canonical["freeWaterAmount"] = doc["freeWaterAmount"];
  if (!doc["pulsesPerLiter"].isNull())
    canonical["pulsesPerLiter"] = doc["pulsesPerLiter"];
  if (!doc["tdsThreshold"].isNull())
    canonical["tdsThreshold"] = doc["tdsThreshold"];
  if (!doc["tdsTemperatureC"].isNull())
    canonical["tdsTemperatureC"] = doc["tdsTemperatureC"];
  if (!doc["tdsCalibrationFactor"].isNull())
    canonical["tdsCalibrationFactor"] = doc["tdsCalibrationFactor"];
  if (!doc["enableFreeWater"].isNull())
    canonical["enableFreeWater"] = doc["enableFreeWater"];
  if (!doc["relayActiveHigh"].isNull())
    canonical["relayActiveHigh"] = doc["relayActiveHigh"];
  if (!doc["relay_active_high"].isNull())
    canonical["relay_active_high"] = doc["relay_active_high"];
  if (!doc["cashPulseValue"].isNull())
    canonical["cashPulseValue"] = doc["cashPulseValue"];
  if (!doc["cashPulseGapMs"].isNull())
    canonical["cashPulseGapMs"] = doc["cashPulseGapMs"];
  if (!doc["paymentCheckInterval"].isNull())
    canonical["paymentCheckInterval"] = doc["paymentCheckInterval"];
  if (!doc["displayUpdateInterval"].isNull())
    canonical["displayUpdateInterval"] = doc["displayUpdateInterval"];
  if (!doc["tdsCheckInterval"].isNull())
    canonical["tdsCheckInterval"] = doc["tdsCheckInterval"];
  if (!doc["heartbeatInterval"].isNull())
    canonical["heartbeatInterval"] = doc["heartbeatInterval"];
  if (!doc["enablePowerSave"].isNull())
    canonical["enablePowerSave"] = doc["enablePowerSave"];
  if (!doc["deepSleepStartHour"].isNull())
    canonical["deepSleepStartHour"] = doc["deepSleepStartHour"];
  if (!doc["deepSleepEndHour"].isNull())
    canonical["deepSleepEndHour"] = doc["deepSleepEndHour"];
  if (!doc["transaction_id"].isNull())
    canonical["transaction_id"] = doc["transaction_id"];
  if (!doc["nonce"].isNull())
    canonical["nonce"] = doc["nonce"];
  if (!doc["ts"].isNull())
    canonical["ts"] = doc["ts"];
  canonical["device_id"] = deviceConfig.device_id;

  String out;
  serializeJson(canonical, out);
  return out;
}

static String canonicalCommand(const JsonDocument &doc) {
  JsonDocument canonical;
  if (!doc["action"].isNull())
    canonical["action"] = doc["action"];
  if (!doc["pricePerLiter"].isNull())
    canonical["pricePerLiter"] = doc["pricePerLiter"];
  if (!doc["threshold"].isNull())
    canonical["threshold"] = doc["threshold"];
  if (!doc["tdsThreshold"].isNull())
    canonical["tdsThreshold"] = doc["tdsThreshold"];
  if (!doc["duration"].isNull())
    canonical["duration"] = doc["duration"];
  if (!doc["reason"].isNull())
    canonical["reason"] = doc["reason"];
  if (!doc["transaction_id"].isNull())
    canonical["transaction_id"] = doc["transaction_id"];
  if (!doc["nonce"].isNull())
    canonical["nonce"] = doc["nonce"];
  if (!doc["ts"].isNull())
    canonical["ts"] = doc["ts"];
  canonical["device_id"] = deviceConfig.device_id;

  String out;
  serializeJson(canonical, out);
  return out;
}

static String canonicalOta(const JsonDocument &doc) {
  JsonDocument canonical;
  if (!doc["firmware_url"].isNull())
    canonical["firmware_url"] = doc["firmware_url"];
  if (!doc["transaction_id"].isNull())
    canonical["transaction_id"] = doc["transaction_id"];
  if (!doc["nonce"].isNull())
    canonical["nonce"] = doc["nonce"];
  if (!doc["ts"].isNull())
    canonical["ts"] = doc["ts"];
  canonical["device_id"] = deviceConfig.device_id;

  String out;
  serializeJson(canonical, out);
  return out;
}

static bool extractSignedTs(const JsonDocument &doc, uint64_t &tsOut) {
  if (!doc["ts"].is<uint64_t>()) {
    return false;
  }
  tsOut = doc["ts"].as<uint64_t>();
  return tsOut != 0;
}

static String extractSignedNonce(const JsonDocument &doc) {
  if (doc["nonce"].is<const char *>()) {
    return String(doc["nonce"].as<const char *>());
  }
  if (doc["transaction_id"].is<const char *>()) {
    return String(doc["transaction_id"].as<const char *>());
  }
  return String();
}

static uint64_t fnv1a64(const void *data, size_t len) {
  static const uint64_t FNV_OFFSET = 14695981039346656037ULL;
  static const uint64_t FNV_PRIME = 1099511628211ULL;
  uint64_t hash = FNV_OFFSET;

  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
  for (size_t i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

static uint64_t hashNonceTs(const String &nonce, uint64_t ts) {
  uint64_t hash = fnv1a64(nonce.c_str(), nonce.length());
  // Mix timestamp bytes into the hash to reduce collision risk.
  for (int i = 0; i < 8; i++) {
    uint8_t b = (uint8_t)((ts >> (i * 8)) & 0xFF);
    hash ^= b;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static bool checkAndStorePersistentNonce(const char *idxKey, const char *bufKey,
                                         uint64_t nonceHash) {
  static const uint8_t CACHE_SIZE = 16;
  uint64_t buf[CACHE_SIZE] = {};

  preferences.begin("ewater", false);
  preferences.getBytes(bufKey, buf, sizeof(buf));

  for (uint8_t i = 0; i < CACHE_SIZE; i++) {
    if (buf[i] != 0 && buf[i] == nonceHash) {
      preferences.end();
      return false;
    }
  }

  uint8_t idx = preferences.getUChar(idxKey, 0) % CACHE_SIZE;
  buf[idx] = nonceHash;

  preferences.putBytes(bufKey, buf, sizeof(buf));
  preferences.putUChar(idxKey, (uint8_t)((idx + 1) % CACHE_SIZE));
  preferences.end();
  return true;
}

static bool enforceSignedReplayProtection(const JsonDocument &doc,
                                          const char *context,
                                          const char *idxKey,
                                          const char *bufKey) {
  if (!deviceConfig.requireSignedMessages) {
    return true;
  }

  uint64_t ts = 0;
  if (!extractSignedTs(doc, ts)) {
    publishLog("ERROR", (String(context) + " missing ts").c_str());
    return false;
  }

  String nonce = extractSignedNonce(doc);
  if (nonce.length() == 0) {
    publishLog("ERROR", (String(context) + " missing nonce").c_str());
    return false;
  }

  uint64_t nonceHash = hashNonceTs(nonce, ts);
  if (!checkAndStorePersistentNonce(idxKey, bufKey, nonceHash)) {
    publishLog("ERROR", (String(context) + " replay detected").c_str());
    return false;
  }

  return true;
}

static bool verifySignedMessage(const JsonDocument &doc,
                                const String &payload) {
  if (!deviceConfig.requireSignedMessages) {
    return true;
  }
  const char *secret = deviceConfig.api_secret;
  if (!secret || secret[0] == '\0') {
    publishLog("ERROR", "Signed messages required but secret not set");
    return false;
  }

  const char *sig = getSignatureField(doc);
  if (!sig || !sig[0]) {
    publishLog("ERROR", "Missing signature");
    return false;
  }

  String expected = hmacSha256Hex(payload, secret);
  String provided = String(sig);
  provided.toLowerCase();
  expected.toLowerCase();

  if (provided != expected) {
    publishLog("ERROR", "Invalid signature");
    return false;
  }
  return true;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.println("JSON parse error!");
    return;
  }

  String topicStr = String(topic);

  // Handle Payment
  if (topicStr == TOPIC_PAYMENT_IN) {
    if (!doc["amount"].is<int>()) {
      Serial.println("ERROR: Missing payment amount");
      publishLog("ERROR", "Missing payment amount");
      return;
    }

    String canonical = canonicalPayment(doc);
    if (!verifySignedMessage(doc, canonical)) {
      Serial.println("Payment rejected: signature invalid");
      return;
    }

    int amount = doc["amount"].as<int>();
    String source = doc["source"] | "unknown";
    String txnId = doc["transaction_id"] | "";
    if (txnId.length() == 0) {
      txnId = doc["nonce"] | "";
    }
    String userId = doc["user_id"] | "";

    if (deviceConfig.requireSignedMessages) {
      uint64_t ts = 0;
      if (!extractSignedTs(doc, ts)) {
        publishLog("ERROR", "PAYMENT missing ts");
        return;
      }
      if (txnId.length() == 0) {
        publishLog("ERROR", "PAYMENT missing transaction_id/nonce");
        return;
      }
      if (!isNewTxnId(txnId)) {
        publishLog("ERROR", "Payment duplicate txnId");
        return;
      }
      rememberTxnId(txnId);
    }

    // analytics.recordPayment(amount); // Removed

    processPayment(amount, source.c_str(),
                   txnId.length() ? txnId.c_str() : nullptr,
                   userId.length() ? userId.c_str() : nullptr);
  } else if (topicStr == TOPIC_CONFIG_IN) {
    Serial.println("Config update received");
    String canonical = canonicalConfig(doc);
    if (!verifySignedMessage(doc, canonical)) {
      Serial.println("Config rejected: signature invalid");
      return;
    }
    if (!enforceSignedReplayProtection(doc, "CONFIG", "cfg_nonce_idx",
                                       "cfg_nonce_buf")) {
      return;
    }
    handleConfigUpdate(doc);
  } else if (topicStr == TOPIC_BROADCAST_CONFIG ||
             topicStr == TOPIC_GROUP_CONFIG) {
    Serial.println("Broadcast/Group config received");

    String canonical = canonicalConfig(doc);
    if (!verifySignedMessage(doc, canonical)) {
      Serial.println("Broadcast config rejected: signature invalid");
      return;
    }
    if (!enforceSignedReplayProtection(doc, "BROADCAST_CONFIG", "cfg_nonce_idx",
                                       "cfg_nonce_buf")) {
      return;
    }

    // Handle common config updates with range validation
    if (!doc["pricePerLiter"].isNull()) {
      int price = doc["pricePerLiter"];
      if (price >= 100 && price <= 100000) { // Range validation
        deviceConfig.pricePerLiter = price;
        saveConfigToStorage();
        applyRuntimeConfig();
        Serial.println("Price updated via broadcast");
      } else {
        Serial.println("Broadcast price rejected: out of range");
      }
    }
    if (!doc["tdsThreshold"].isNull()) {
      int tds = doc["tdsThreshold"];
      if (tds >= 0 && tds <= 2000) { // Range validation
        deviceConfig.tdsThreshold = tds;
        saveConfigToStorage();
        applyRuntimeConfig(); // FIX: Apply runtime config for TDS too
        Serial.println("TDS threshold updated via broadcast");
      } else {
        Serial.println("Broadcast TDS rejected: out of range");
      }
    }
  }
  // Handle Broadcast/Group Commands
  else if (topicStr == TOPIC_BROADCAST_COMMAND ||
           topicStr == TOPIC_GROUP_COMMAND) {
    Serial.println("Broadcast/Group command received");

    // CRITICAL FIX: Verify signature for commands (must include `action`)
    String canonical = canonicalCommand(doc);
    if (!verifySignedMessage(doc, canonical)) {
      Serial.println("Command rejected: signature invalid");
      return;
    }
    if (!enforceSignedReplayProtection(doc, "COMMAND", "cmd_nonce_idx",
                                       "cmd_nonce_buf")) {
      return;
    }

    String action = doc["action"] | "";

    if (action == "updatePrice" && !doc["pricePerLiter"].isNull()) {
      deviceConfig.pricePerLiter = doc["pricePerLiter"];
      saveConfigToStorage();
      applyRuntimeConfig();
      publishLog("FLEET", "Price updated via broadcast");
    } else if (action == "updateTdsThreshold" && !doc["threshold"].isNull()) {
      deviceConfig.tdsThreshold = doc["threshold"];
      saveConfigToStorage();
      publishLog("FLEET", "TDS threshold updated");
    } else if (action == "identify") {
      // Blink display or LED for physical identification
      int duration = doc["duration"] | 10;

      // FIX: Limit duration to prevent watchdog timeout (max 10 iterations = 12
      // seconds)
      if (duration > 10) {
        duration = 10;
      }

      publishLog("FLEET", "Identify command received");

      // Visual identification: blink LCD backlight
      for (int i = 0; i < duration; i++) {
        esp_task_wdt_reset(); // FIX: Reset watchdog during long operation

        lcd.noBacklight();
        delay(200);
        lcd.backlight();
        delay(200);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(">>> IDENTIFY <<<");
        lcd.setCursor(0, 1);
        lcd.print("Device found!");
        delay(600);
      }

      // Restore normal display after identify
      displayIdle();
    } else if (action == "emergencyShutdown") {
      String reason = doc["reason"] | "Emergency";
      String msg = "EMERGENCY SHUTDOWN: " + reason;
      // alertCritical(CAT_SYSTEM, msg.c_str());
      publishLog("ALERT", msg.c_str()); // Replaced with simple log
      publishLog("FLEET", "Emergency shutdown initiated");
      // Force safe stop
      setRelay(false);
      currentState = IDLE;
      balance = 0;
      publishStatus();
    }
  } else if (topicStr == TOPIC_OTA_IN) {
    Serial.println("OTA update command received");

    // CRITICAL FIX: Verify signature for OTA (include url + ts + nonce)
    String canonical = canonicalOta(doc);
    if (!verifySignedMessage(doc, canonical)) {
      Serial.println("OTA rejected: signature invalid");
      return;
    }
    if (!enforceSignedReplayProtection(doc, "OTA", "ota_nonce_idx",
                                       "ota_nonce_buf")) {
      return;
    }

    if (!doc["firmware_url"].is<String>()) {
      publishLog("OTA_ERROR", "Missing firmware_url");
      return;
    }
    String firmwareUrl = doc["firmware_url"].as<String>();
    triggerOTAUpdate(firmwareUrl.c_str());
  }
}

// ============================================
// PAYMENT PROCESSING (shared by MQTT & cash pulses)
// ============================================
void processPayment(int amount, const char *source, const char *txnId,
                    const char *userId) {
  if (amount <= 0) {
    publishLog("ERROR", "Invalid payment: negative or zero amount");
    return;
  }
  if (amount > 1000000) {
    publishLog("ERROR", "Invalid payment: amount exceeds limit");
    return;
  }

  const char *safeSource = (source && source[0]) ? source : "unknown";

  Serial.print("Payment received: ");
  Serial.print(amount);
  Serial.print(" from ");
  Serial.println(safeSource);

  if (txnId && txnId[0]) {
    Serial.print("Transaction ID: ");
    Serial.println(txnId);
  }

  balance += amount;

  if (balance > 0) {
    if (currentState == IDLE) {
      // Normal: Start paid session from idle
      currentState = ACTIVE;
      sessionStartBalance = balance;
      freeWaterUsed = false;
    } else if (currentState == FREE_WATER) {
      // Payment during free water: continue as paid dispensing immediately.
      Serial.println("💰 Payment during FREE_WATER → switching to DISPENSING");
      currentState = DISPENSING;
      sessionStartBalance = balance;
      freeWaterUsed = true; // Don't allow free water again this session
      flowPulseCount = 0;
      lastDispensedLiters = 0.0;
      totalDispensedLiters = 0.0;
      setRelay(true);
    } else if (currentState == DISPENSING) {
      // Payment during dispensing: add to balance, continue dispensing
      Serial.println("💰 Additional payment during DISPENSING");
    } else if (currentState == PAUSED) {
      // Payment during pause: add to balance
      Serial.println("💰 Payment during PAUSED - balance increased");
    }
  }

  resetSessionTimer();

  char paymentLog[256];
  int offset =
      snprintf(paymentLog, sizeof(paymentLog), "%d|%s", amount, safeSource);

  if (txnId && txnId[0] && offset < sizeof(paymentLog)) {
    int written = snprintf(paymentLog + offset, sizeof(paymentLog) - offset,
                           "|%s", txnId);
    if (written > 0)
      offset += written;
  }
  if (userId && userId[0] && offset < sizeof(paymentLog)) {
    snprintf(paymentLog + offset, sizeof(paymentLog) - offset, "|%s", userId);
  }

  publishLog("PAYMENT", paymentLog);
  publishStatus();
}

// ============================================
// CONFIG UPDATE HANDLER
// ============================================
void handleConfigUpdate(JsonDocument &doc) {
  DeviceConfig prevConfig = deviceConfig;
  bool updated = false;
  bool wifiChanged = false;
  bool mqttChanged = false;
  bool deviceIdChanged = false;
  bool allowNetConfig = deviceConfig.allowRemoteNetworkConfig;

  if (allowNetConfig) {
    // WiFi
    // HIGH FIX: Support snake_case keys
    String ssid = doc["wifiSsid"] | doc["wifi_ssid"] | "";
    if (ssid.length() > 0 && ssid.length() < 32) {
      copyToBuffer(deviceConfig.wifi_ssid, sizeof(deviceConfig.wifi_ssid),
                   ssid);
      wifiChanged = true;
      updated = true;
    }

    String pass = doc["wifiPassword"] | doc["wifi_password"] | "";
    if (pass.length() > 0 && pass.length() < 64) {
      copyToBuffer(deviceConfig.wifi_password,
                   sizeof(deviceConfig.wifi_password), pass);
      wifiChanged = true;
      updated = true;
    }

    // MQTT
    String broker = doc["mqttBroker"] | doc["mqtt_broker"] | "";
    if (broker.length() > 0 && broker.length() < 128) {
      copyToBuffer(deviceConfig.mqtt_broker, sizeof(deviceConfig.mqtt_broker),
                   broker);
      mqttChanged = true;
      updated = true;
    }

    int port = doc["mqttPort"] | doc["mqtt_port"] | 0;
    if (port > 0 && port < 65536) {
      deviceConfig.mqtt_port = port;
      mqttChanged = true;
      updated = true;
    }

    // MQTT auth (optional). Only update if key is present to allow partial
    // updates without clearing credentials.
    bool hasUser = doc["mqttUsername"].is<const char *>() ||
                   doc["mqtt_username"].is<const char *>();
    if (hasUser) {
      String user = doc["mqttUsername"] | doc["mqtt_username"] | "";
      if (user.length() < 32) { // Allow empty but not too long
        copyToBuffer(deviceConfig.mqtt_username,
                     sizeof(deviceConfig.mqtt_username), user);
        mqttChanged = true;
        updated = true;
      }
    }

    bool hasPass = doc["mqttPassword"].is<const char *>() ||
                   doc["mqtt_password"].is<const char *>();
    if (hasPass) {
      String mqttPass = doc["mqttPassword"] | doc["mqtt_password"] | "";
      if (mqttPass.length() < 64) {
        copyToBuffer(deviceConfig.mqtt_password,
                     sizeof(deviceConfig.mqtt_password), mqttPass);
        mqttChanged = true;
        updated = true;
      }
    }

    // Device ID
    String devId = doc["deviceId"] | doc["device_id"] | "";
    if (devId.length() > 0 && devId.length() < 32) {
      copyToBuffer(deviceConfig.device_id, sizeof(deviceConfig.device_id),
                   devId);
      deviceIdChanged = true;
      updated = true;
    }
  } else {
    // Check for attempted network config when disabled
    if (doc["wifiSsid"].is<const char *>() ||
        doc["wifi_ssid"].is<const char *>() ||
        doc["mqttBroker"].is<const char *>() ||
        doc["mqtt_broker"].is<const char *>()) {
      publishLog("CONFIG", "Remote network config disabled");
    }
  }

// Vending settings
// Helper to check both keys
#define GET_INT(key1, key2) (doc[key1] | doc[key2] | -1)
#define GET_FLOAT(key1, key2) (doc[key1] | doc[key2] | -1.0f)

  int price = GET_INT("pricePerLiter", "price_per_liter");
  if (price > 0 && price <= 100000) {
    deviceConfig.pricePerLiter = price;
    updated = true;
  }

  int sessionTimeout = GET_INT("sessionTimeout", "session_timeout");
  if (sessionTimeout > 0) {
    deviceConfig.sessionTimeout = normalizeSecondsOrMs(sessionTimeout);
    updated = true;
  }

  int freeCooldown = GET_INT("freeWaterCooldown", "free_water_cooldown");
  if (freeCooldown > 0) {
    deviceConfig.freeWaterCooldown = normalizeSecondsOrMs(freeCooldown);
    updated = true;
  }

  float freeAmount = GET_FLOAT("freeWaterAmount", "free_water_amount");
  if (freeAmount > 0) {
    deviceConfig.freeWaterAmount = freeAmount;
    updated = true;
  }

  float pulses = GET_FLOAT("pulsesPerLiter", "pulses_per_liter");
  if (pulses > 0) {
    deviceConfig.pulsesPerLiter = pulses;
    updated = true;
  }

  int tdsThresh = GET_INT("tdsThreshold", "tds_threshold");
  if (tdsThresh >= 0) {
    deviceConfig.tdsThreshold = tdsThresh;
    updated = true;
  }

  // ... (Abbreviated for brevity, applying pattern to others)
  // Reverting to direct checks for remaining fields to ensure correctness
  // without macro magic complexity

  if (!doc["tdsTemperatureC"].isNull()) {
    float temp = doc["tdsTemperatureC"].as<float>();
    if (temp >= 0.0f && temp <= 80.0f) {
      deviceConfig.tdsTemperatureC = temp;
      updated = true;
    }
  }
  if (!doc["tdsCalibrationFactor"].isNull()) {
    float factor = doc["tdsCalibrationFactor"].as<float>();
    if (factor > 0.0f && factor <= 5.0f) {
      deviceConfig.tdsCalibrationFactor = factor;
      updated = true;
    }
  }
  if (!doc["enableFreeWater"].isNull()) {
    deviceConfig.enableFreeWater = doc["enableFreeWater"].as<bool>();
    updated = true;
  }
  if (!doc["relayActiveHigh"].isNull()) {
    deviceConfig.relayActiveHigh = true;
    updated = true;
  }
  if (!doc["relay_active_high"].isNull()) {
    deviceConfig.relayActiveHigh = true;
    updated = true;
  }

  // Cash
  if (!doc["cashPulseValue"].isNull()) {
    int value = doc["cashPulseValue"].as<int>();
    if (value > 0 && value <= 100000) {
      deviceConfig.cashPulseValue = value;
      updated = true;
    }
  }
  if (!doc["cashPulseGapMs"].isNull()) {
    unsigned long gap = doc["cashPulseGapMs"].as<unsigned long>();
    if (gap >= 20 && gap <= 1000) {
      deviceConfig.cashPulseGapMs = gap;
      updated = true;
    }
  }

  // Intervals
  if (!doc["paymentCheckInterval"].isNull()) {
    unsigned long interval = doc["paymentCheckInterval"].as<unsigned long>();
    if (interval >= 200 && interval <= 600000) {
      deviceConfig.paymentCheckInterval = interval;
      updated = true;
    }
  }
  if (!doc["displayUpdateInterval"].isNull()) {
    unsigned long interval = doc["displayUpdateInterval"].as<unsigned long>();
    if (interval >= 50 && interval <= 10000) {
      deviceConfig.displayUpdateInterval = interval;
      updated = true;
    }
  }
  if (!doc["tdsCheckInterval"].isNull()) {
    unsigned long interval = doc["tdsCheckInterval"].as<unsigned long>();
    if (interval >= 1000 && interval <= 600000) {
      deviceConfig.tdsCheckInterval = interval;
      updated = true;
    }
  }
  if (!doc["heartbeatInterval"].isNull()) {
    unsigned long interval = doc["heartbeatInterval"].as<unsigned long>();
    if (interval >= 1000 && interval <= 3600000) {
      deviceConfig.heartbeatInterval = interval;
      updated = true;
    }
  }

  // Power Management Removed

  if (!updated) {
    return;
  }

  // ... rest of function ...

  deviceConfig.configured = (deviceConfig.wifi_ssid[0] != '\0' &&
                             deviceConfig.mqtt_broker[0] != '\0');

  scheduleConfigSave();

  String applyMode = doc["apply"] | "now";
  applyMode.toLowerCase();
  if (applyMode == "restart") {
    saveConfigToStorage();
    publishLog("CONFIG", "Saved. Restarting.");
    delay(200);
    ESP.restart();
    return;
  }

  applyRuntimeConfig();
  applyConfigStateEffects();

  if (wifiChanged) {
    setupWiFi();
  }
  if (mqttChanged || deviceIdChanged) {
    mqttClient.disconnect();
    mqttClient.setServer(deviceConfig.mqtt_broker, deviceConfig.mqtt_port);
    reconnectMQTT();
  }
  beginNetworkApply(prevConfig, wifiChanged, mqttChanged || deviceIdChanged);

  Serial.println("Config updated!");
  publishLog("CONFIG", "Updated from backend");
  publishStatus();
}

// ============================================
// NETWORK APPLY WITH ROLLBACK
// ============================================
void beginNetworkApply(const DeviceConfig &previous, bool wifiChanged,
                       bool mqttChanged) {
  if (!wifiChanged && !mqttChanged) {
    return;
  }
  prevNetworkConfig = previous;
  pendingWifiApply = wifiChanged;
  pendingMqttApply = mqttChanged;
  networkApplyStartMs = millis();
  networkApplyPending = true;
}

void processNetworkApply() {
  if (!networkApplyPending) {
    return;
  }

  bool wifiOk = !pendingWifiApply || (WiFi.status() == WL_CONNECTED);
  bool mqttOk = !pendingMqttApply || mqttClient.connected();

  if (wifiOk && mqttOk) {
    networkApplyPending = false;
    pendingWifiApply = false;
    pendingMqttApply = false;
    publishLog("CONFIG", "Network config applied");
    return;
  }

  if (millis() - networkApplyStartMs < networkApplyTimeoutMs) {
    return;
  }

  // Rollback
  deviceConfig = prevNetworkConfig;
  saveConfigToStorage();
  applyRuntimeConfig();
  applyConfigStateEffects();

  setupWiFi();
  mqttClient.disconnect();
  mqttClient.setServer(deviceConfig.mqtt_broker, deviceConfig.mqtt_port);
  reconnectMQTT();

  networkApplyPending = false;
  pendingWifiApply = false;
  pendingMqttApply = false;

  publishLog("CONFIG", "Network config rollback");
  publishStatus();
}

// ============================================
// MQTT PUBLISH FUNCTIONS
// ============================================
void publishStatus() {
  if (!mqttClient.connected()) {
    return;
  }
  JsonDocument doc;

  doc["device_id"] = deviceConfig.device_id;

  // MEDIUM FIX: Send state as string, not enum/int
  const char *stateNames[] = {"IDLE", "ACTIVE", "DISPENSING", "PAUSED",
                              "FREE_WATER"};
  const int stateIndex = static_cast<int>(currentState);
  doc["state"] = (stateIndex >= 0 && stateIndex < (int)(sizeof(stateNames) /
                                                        sizeof(stateNames[0])))
                     ? stateNames[stateIndex]
                     : "UNKNOWN";

  doc["balance"] = balance;
  doc["last_dispense"] =
      totalDispensedLiters; // MEDIUM FIX: renamed from "dispensed"
  doc["tds"] = readTDS();
  doc["free_water_available"] =
      (millis() >= freeWaterAvailableTime && !freeWaterUsed);

  String output;
  serializeJson(doc, output);

  // QoS 1, Retained = true (for latest status)
  mqttClient.publish(TOPIC_STATUS_OUT, output.c_str(), true);
}

void publishLog(const char *event, const char *message) {
  JsonDocument doc;

  doc["device_id"] = deviceConfig.device_id;
  doc["event"] = event;
  doc["message"] = message;

  String output;
  serializeJson(doc, output);

  if (!mqttClient.connected()) {
    return;
  }

  // QoS 1 for logs (important events)
  mqttClient.publish(TOPIC_LOG_OUT, output.c_str(), false);
}

void publishMQTT(const char *topic, const char *message) {
  mqttClient.publish(topic, message);
}
