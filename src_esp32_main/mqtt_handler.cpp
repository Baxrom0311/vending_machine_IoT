#include "mqtt_handler.h"
#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "display.h"
#include "relay_control.h"
#include "sensors.h"
#include "state_machine.h"
#include "uart_receiver.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>
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
static const uint16_t kMqttSocketTimeoutSec = 8;
// Minimal inbound MQTT profile:
// - Keep payment topic enabled
// - Disable remote config/fleet control by default
static const bool kMqttInboundConfigEnabled = false;
static const bool kMqttInboundFleetEnabled = false;

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
      512); // Payment-only inbound profile does not require large config payloads
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(kMqttSocketTimeoutSec);
  espClient.setTimeout(kMqttSocketTimeoutSec);

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

  DEBUG_PRINT("Connecting to MQTT (attempt ");
  DEBUG_PRINT(failedAttempts + 1);
  DEBUG_PRINT("): ");
  DEBUG_PRINT(deviceConfig.mqtt_broker);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(deviceConfig.mqtt_port);

  const char *clientId = deviceConfig.device_id;
  const char *username =
      deviceConfig.mqtt_username[0] ? deviceConfig.mqtt_username : nullptr;
  const char *password =
      deviceConfig.mqtt_password[0] ? deviceConfig.mqtt_password : nullptr;

  if (mqttClient.connect(clientId, username, password)) {
    DEBUG_PRINTLN("MQTT Connected!");
    failedAttempts = 0; // Reset counter on success

    // Subscribe to topics (minimal inbound profile)
    mqttClient.subscribe(TOPIC_PAYMENT_IN);

    if (kMqttInboundConfigEnabled) {
      mqttClient.subscribe(TOPIC_CONFIG_IN);
    }

    if (kMqttInboundFleetEnabled) {
      // Subscribe to broadcast topics (all devices)
      mqttClient.subscribe(TOPIC_BROADCAST_CONFIG);
      mqttClient.subscribe(TOPIC_BROADCAST_COMMAND);

      // Subscribe to group topics (if groupId is set)
      if (strlen(deviceConfig.groupId) > 0) {
        mqttClient.subscribe(TOPIC_GROUP_CONFIG);
        mqttClient.subscribe(TOPIC_GROUP_COMMAND);
      }
    }

    DEBUG_PRINTLN("Subscribed to MQTT inbound profile");

    // Publish online status
    publishLog("MQTT", "Connected");
  } else {
    failedAttempts++; // Increment for backoff calculation
    // analytics.incrementMqttReconnects(); // Removed
    DEBUG_PRINT("Failed, rc=");
    DEBUG_PRINT(mqttClient.state());
    DEBUG_PRINT(", next retry in ");
    DEBUG_PRINT(
        backoffDelays[(failedAttempts < maxBackoffIndex) ? failedAttempts
                                                         : maxBackoffIndex] /
        1000);
    DEBUG_PRINTLN(" seconds");
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

  auto copyAlias = [&](const char *dstKey, const char *keyA,
                       const char *keyB = nullptr) {
    if (!doc[keyA].isNull()) {
      canonical[dstKey] = doc[keyA];
      return;
    }
    if (keyB && !doc[keyB].isNull()) {
      canonical[dstKey] = doc[keyB];
    }
  };

  copyAlias("apply", "apply");
  copyAlias("deviceId", "deviceId", "device_id");
  copyAlias("wifiSsid", "wifiSsid", "wifi_ssid");
  copyAlias("wifiPassword", "wifiPassword", "wifi_password");
  copyAlias("mqttBroker", "mqttBroker", "mqtt_broker");
  copyAlias("mqttPort", "mqttPort", "mqtt_port");
  copyAlias("mqttUsername", "mqttUsername", "mqtt_username");
  copyAlias("mqttPassword", "mqttPassword", "mqtt_password");
  copyAlias("pricePerLiter", "pricePerLiter", "price_per_liter");
  copyAlias("sessionTimeout", "sessionTimeout", "session_timeout");
  copyAlias("freeWaterCooldown", "freeWaterCooldown", "free_water_cooldown");
  copyAlias("freeWaterAmount", "freeWaterAmount", "free_water_amount");
  copyAlias("pulsesPerLiter", "pulsesPerLiter", "pulses_per_liter");
  copyAlias("tdsThreshold", "tdsThreshold", "tds_threshold");
  copyAlias("tdsTemperatureC", "tdsTemperatureC", "tds_temperature_c");
  copyAlias("tdsCalibrationFactor", "tdsCalibrationFactor",
            "tds_calibration_factor");
  copyAlias("enableFreeWater", "enableFreeWater", "enable_free_water");
  copyAlias("relayActiveHigh", "relayActiveHigh", "relay_active_high");
  copyAlias("cashPulseValue", "cashPulseValue", "cash_pulse_value");
  copyAlias("cashPulseGapMs", "cashPulseGapMs", "cash_pulse_gap_ms");
  copyAlias("paymentCheckInterval", "paymentCheckInterval",
            "payment_check_interval");
  copyAlias("displayUpdateInterval", "displayUpdateInterval",
            "display_update_interval");
  copyAlias("tdsCheckInterval", "tdsCheckInterval", "tds_check_interval");
  copyAlias("heartbeatInterval", "heartbeatInterval", "heartbeat_interval");
  copyAlias("enablePowerSave", "enablePowerSave", "enable_power_save");
  copyAlias("deepSleepStartHour", "deepSleepStartHour",
            "deep_sleep_start_hour");
  copyAlias("deepSleepEndHour", "deepSleepEndHour", "deep_sleep_end_hour");
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
  if (!doc["action"].isNull()) {
    canonical["action"] = doc["action"];
  }
  if (!doc["threshold"].isNull()) {
    canonical["threshold"] = doc["threshold"];
  }
  if (!doc["tdsThreshold"].isNull()) {
    canonical["tdsThreshold"] = doc["tdsThreshold"];
  } else if (!doc["tds_threshold"].isNull()) {
    canonical["tdsThreshold"] = doc["tds_threshold"];
  }
  if (!doc["reason"].isNull()) {
    canonical["reason"] = doc["reason"];
  }
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
  DEBUG_PRINT("Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("]: ");

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    DEBUG_PRINTLN("JSON parse error!");
    return;
  }

  String topicStr = String(topic);

  // Handle Payment
  if (topicStr == TOPIC_PAYMENT_IN) {
    if (!doc["amount"].is<int>()) {
      DEBUG_PRINTLN("ERROR: Missing payment amount");
      publishLog("ERROR", "Missing payment amount");
      return;
    }

    String canonical = canonicalPayment(doc);
    if (!verifySignedMessage(doc, canonical)) {
      DEBUG_PRINTLN("Payment rejected: signature invalid");
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
    if (!kMqttInboundConfigEnabled) {
      DEBUG_PRINTLN("Config topic ignored (disabled)");
      return;
    }
    DEBUG_PRINTLN("Config update received");
    String canonical = canonicalConfig(doc);
    if (!verifySignedMessage(doc, canonical)) {
      DEBUG_PRINTLN("Config rejected: signature invalid");
      return;
    }
    if (!enforceSignedReplayProtection(doc, "CONFIG", "cfg_nonce_idx",
                                       "cfg_nonce_buf")) {
      return;
    }
    handleConfigUpdate(doc);
  } else if (topicStr == TOPIC_BROADCAST_CONFIG ||
             topicStr == TOPIC_GROUP_CONFIG) {
    if (!kMqttInboundFleetEnabled) {
      DEBUG_PRINTLN("Fleet config topic ignored (disabled)");
      return;
    }
    DEBUG_PRINTLN("Broadcast/Group config received");

    String canonical = canonicalConfig(doc);
    if (!verifySignedMessage(doc, canonical)) {
      DEBUG_PRINTLN("Broadcast config rejected: signature invalid");
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
        scheduleConfigSave();
        applyRuntimeConfig();
        DEBUG_PRINTLN("Price updated via broadcast");
      } else {
        DEBUG_PRINTLN("Broadcast price rejected: out of range");
      }
    }
    if (!doc["tdsThreshold"].isNull()) {
      int tds = doc["tdsThreshold"];
      if (tds >= 0 && tds <= 2000) { // Range validation
        deviceConfig.tdsThreshold = tds;
        scheduleConfigSave();
        applyRuntimeConfig(); // FIX: Apply runtime config for TDS too
        DEBUG_PRINTLN("TDS threshold updated via broadcast");
      } else {
        DEBUG_PRINTLN("Broadcast TDS rejected: out of range");
      }
    }
  }
  // Handle Broadcast/Group Commands
  else if (topicStr == TOPIC_BROADCAST_COMMAND ||
           topicStr == TOPIC_GROUP_COMMAND) {
    if (!kMqttInboundFleetEnabled) {
      DEBUG_PRINTLN("Fleet command topic ignored (disabled)");
      return;
    }
    DEBUG_PRINTLN("Broadcast/Group command received");

    // CRITICAL FIX: Verify signature for commands (must include `action`)
    String canonical = canonicalCommand(doc);
    if (!verifySignedMessage(doc, canonical)) {
      DEBUG_PRINTLN("Command rejected: signature invalid");
      return;
    }
    if (!enforceSignedReplayProtection(doc, "COMMAND", "cmd_nonce_idx",
                                       "cmd_nonce_buf")) {
      return;
    }

    String action = doc["action"] | "";

    if (action == "updateTdsThreshold" && !doc["threshold"].isNull()) {
      deviceConfig.tdsThreshold = doc["threshold"];
      scheduleConfigSave();
      publishLog("FLEET", "TDS threshold updated");
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
    } else {
      publishLog("FLEET", "Unsupported command action");
    }
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

  DEBUG_PRINT("Payment received: ");
  DEBUG_PRINT(amount);
  DEBUG_PRINT(" from ");
  DEBUG_PRINTLN(safeSource);

  if (txnId && txnId[0]) {
    DEBUG_PRINT("Transaction ID: ");
    DEBUG_PRINTLN(txnId);
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
      DEBUG_PRINTLN("💰 Payment during FREE_WATER → switching to DISPENSING");
      currentState = DISPENSING;
      sessionStartBalance = balance;
      freeWaterUsed = true; // Don't allow free water again this session
      resetFlowCounters();
      totalDispensedLiters = 0.0;
      setRelay(true);
    } else if (currentState == DISPENSING) {
      // Payment during dispensing: add to balance, continue dispensing
      DEBUG_PRINTLN("💰 Additional payment during DISPENSING");
    } else if (currentState == PAUSED) {
      // Payment during pause: add to balance
      DEBUG_PRINTLN("💰 Payment during PAUSED - balance increased");
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

  } else {
    // Check for attempted network config when disabled
    if (doc["wifiSsid"].is<const char *>() ||
        doc["wifi_ssid"].is<const char *>() ||
        doc["mqttBroker"].is<const char *>() ||
        doc["mqtt_broker"].is<const char *>()) {
      publishLog("CONFIG", "Remote network config disabled");
    }
  }

  // Remote config hardening:
  // Allow only operational settings via MQTT.
  // Calibration/hardware-sensitive fields remain Serial-only.
  const bool hasRestrictedKeys =
      !doc["deviceId"].isNull() || !doc["device_id"].isNull() ||
      !doc["pulsesPerLiter"].isNull() || !doc["pulses_per_liter"].isNull() ||
      !doc["tdsTemperatureC"].isNull() || !doc["tdsCalibrationFactor"].isNull() ||
      !doc["relayActiveHigh"].isNull() || !doc["relay_active_high"].isNull() ||
      !doc["cashPulseValue"].isNull() || !doc["cashPulseGapMs"].isNull() ||
      !doc["paymentCheckInterval"].isNull() ||
      !doc["displayUpdateInterval"].isNull() || !doc["tdsCheckInterval"].isNull();
  if (hasRestrictedKeys) {
    publishLog("CONFIG", "Restricted fields ignored (serial-only)");
  }

  // Vending settings
  // Helper to check both keys
#define GET_INT(key1, key2) (doc[key1] | doc[key2] | -1)
#define GET_FLOAT(key1, key2) (doc[key1] | doc[key2] | -1.0f)

  int price = GET_INT("pricePerLiter", "price_per_liter");
  if (price >= 100 && price <= 100000) {
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

  int tdsThresh = GET_INT("tdsThreshold", "tds_threshold");
  if (tdsThresh >= 0 && tdsThresh <= 2000) {
    deviceConfig.tdsThreshold = tdsThresh;
    updated = true;
  }

  if (!doc["enableFreeWater"].isNull()) {
    deviceConfig.enableFreeWater = doc["enableFreeWater"].as<bool>();
    updated = true;
  }
  if (!doc["heartbeatInterval"].isNull()) {
    unsigned long interval = doc["heartbeatInterval"].as<unsigned long>();
    if (interval >= 5000 && interval <= 3600000) {
      deviceConfig.heartbeatInterval = interval;
      updated = true;
    }
  }

  // Power Management Removed

#undef GET_INT
#undef GET_FLOAT

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
  if (isPaymentEspConnected()) {
    sendCashConfigToPaymentEsp(config.cashPulseValue, config.cashPulseGapMs);
  }

  if (wifiChanged) {
    setupWiFi();
  }
  if (mqttChanged) {
    mqttClient.disconnect();
    mqttClient.setServer(deviceConfig.mqtt_broker, deviceConfig.mqtt_port);
    reconnectMQTT();
  }
  beginNetworkApply(prevConfig, wifiChanged, mqttChanged);

  DEBUG_PRINTLN("Config updated!");
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

  // PubSubClient publish uses QoS 0. Retain latest status for dashboards.
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

  // PubSubClient publish uses QoS 0 (best-effort).
  mqttClient.publish(TOPIC_LOG_OUT, output.c_str(), false);
}

void publishMQTT(const char *topic, const char *message) {
  mqttClient.publish(topic, message);
}
