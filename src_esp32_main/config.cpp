#include "config.h"
#include "config_storage.h"
#include "display.h"
#include <WiFi.h>
#include <cstdio>

// ============================================
// GLOBAL CONFIG INSTANCE (from deviceConfig)
// ============================================
Config config;

// ============================================
// MQTT TOPICS (Generated dynamically)
// ============================================
char TOPIC_PAYMENT_IN[64];
char TOPIC_STATUS_OUT[64];
char TOPIC_CONFIG_IN[64];
char TOPIC_LOG_OUT[64];
char TOPIC_TDS_OUT[64];
char TOPIC_HEARTBEAT[64];
char TOPIC_OTA_IN[64]; // OTA firmware update
char TOPIC_TELEMETRY[64];
char TOPIC_ALERTS[64];
char TOPIC_DIAGNOSTICS[64];

// Fleet Management Topics
char TOPIC_BROADCAST_CONFIG[64];
char TOPIC_BROADCAST_COMMAND[64];
char TOPIC_GROUP_CONFIG[64];
char TOPIC_GROUP_COMMAND[64];

// ============================================
// WIFI SETUP
// ============================================

enum WiFiConnectState {
  WIFI_IDLE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
};
static WiFiConnectState wifiState = WIFI_IDLE;
static unsigned long wifiStartMs = 0;
static unsigned long wifiRetryMs = 0;

static void printWiFiStatus(const char *message) {
  lcd.setCursor(0, 1);
  lcd.print("WiFi: ");
  lcd.print(message);
  // Clear rest of line
  for (int i = strlen("WiFi: ") + strlen(message); i < LCD_COLS; i++) {
    lcd.print(' ');
  }
}

static void startWiFiConnect() {
  if (deviceConfig.wifi_ssid[0] == '\0') {
    Serial.println("WiFi not configured!");
    printWiFiStatus("Not configured");
    wifiState = WIFI_FAILED;
    return;
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(deviceConfig.wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.begin(deviceConfig.wifi_ssid, deviceConfig.wifi_password);

  wifiState = WIFI_CONNECTING;
  wifiStartMs = millis();
  printWiFiStatus("Connecting...");
}

void setupWiFi() { startWiFiConnect(); }

void processWiFi() {
  if (wifiState == WIFI_CONNECTED && WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    printWiFiStatus("Disconnected");
    wifiState = WIFI_FAILED;
    wifiRetryMs = millis();
  }

  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      printWiFiStatus("Connected");
      wifiState = WIFI_CONNECTED;
      return;
    }
    if (millis() - wifiStartMs > 10000) {
      Serial.println("WiFi connect timeout");
      printWiFiStatus("Failed");
      wifiState = WIFI_FAILED;
      wifiRetryMs = millis();
      return;
    }
  }

  if (wifiState == WIFI_FAILED) {
    if (deviceConfig.wifi_ssid[0] == '\0') {
      return;
    }
    if (millis() - wifiRetryMs > 10000) {
      startWiFiConnect();
    }
  }
}

// ============================================
// CONFIG APPLY (Runtime)
// ============================================
void applyRuntimeConfig() {
  config.pricePerLiter = deviceConfig.pricePerLiter;
  config.sessionTimeout = deviceConfig.sessionTimeout;
  config.freeWaterCooldown = deviceConfig.freeWaterCooldown;
  config.freeWaterAmount = deviceConfig.freeWaterAmount;
  config.pulsesPerLiter = deviceConfig.pulsesPerLiter;
  config.tdsThreshold = deviceConfig.tdsThreshold;
  config.tdsTemperatureC = deviceConfig.tdsTemperatureC;
  config.tdsCalibrationFactor = deviceConfig.tdsCalibrationFactor;
  config.enableFreeWater = deviceConfig.enableFreeWater;
  // Hardware policy: relay is fixed Active-HIGH.
  deviceConfig.relayActiveHigh = true;
  config.relayActiveHigh = true;
  config.cashPulseValue = deviceConfig.cashPulseValue;
  config.cashPulseGapMs = deviceConfig.cashPulseGapMs;
  config.paymentCheckInterval = deviceConfig.paymentCheckInterval;
  config.displayUpdateInterval = deviceConfig.displayUpdateInterval;
  config.tdsCheckInterval = deviceConfig.tdsCheckInterval;
  config.heartbeatInterval = deviceConfig.heartbeatInterval;

  generateMQTTTopics();
}

// ============================================
// CONFIG INITIALIZATION
// ============================================
void initConfig() {
  applyRuntimeConfig();
  Serial.println("Config initialized from storage");
}

// ============================================
// MQTT TOPIC GENERATION
// ============================================
void generateMQTTTopics() {
  const char *deviceId = deviceConfig.device_id;
  if (!deviceId || deviceId[0] == '\0') {
    deviceId = "device_001";
  }

  snprintf(TOPIC_PAYMENT_IN, sizeof(TOPIC_PAYMENT_IN), "vending/%s/payment/in",
           deviceId);
  snprintf(TOPIC_STATUS_OUT, sizeof(TOPIC_STATUS_OUT), "vending/%s/status/out",
           deviceId);
  snprintf(TOPIC_CONFIG_IN, sizeof(TOPIC_CONFIG_IN), "vending/%s/config/in",
           deviceId);
  snprintf(TOPIC_LOG_OUT, sizeof(TOPIC_LOG_OUT), "vending/%s/log/out",
           deviceId);
  snprintf(TOPIC_TDS_OUT, sizeof(TOPIC_TDS_OUT), "vending/%s/tds/out",
           deviceId);
  snprintf(TOPIC_HEARTBEAT, sizeof(TOPIC_HEARTBEAT), "vending/%s/heartbeat",
           deviceId);
  snprintf(TOPIC_OTA_IN, sizeof(TOPIC_OTA_IN), "vending/%s/ota/in", deviceId);
  snprintf(TOPIC_TELEMETRY, sizeof(TOPIC_TELEMETRY), "vending/%s/telemetry",
           deviceId);
  snprintf(TOPIC_ALERTS, sizeof(TOPIC_ALERTS), "vending/%s/alerts", deviceId);
  snprintf(TOPIC_DIAGNOSTICS, sizeof(TOPIC_DIAGNOSTICS),
           "vending/%s/diagnostics", deviceId);

  // Broadcast topics (all devices)
  // Broadcast topics (all devices)
  snprintf(TOPIC_BROADCAST_CONFIG, sizeof(TOPIC_BROADCAST_CONFIG),
           "vending/broadcast/config");
  snprintf(TOPIC_BROADCAST_COMMAND, sizeof(TOPIC_BROADCAST_COMMAND),
           "vending/broadcast/command");

  // Group topics (if groupId is set)
  if (strlen(deviceConfig.groupId) > 0) {
    snprintf(TOPIC_GROUP_CONFIG, sizeof(TOPIC_GROUP_CONFIG),
             "vending/group/%s/config", deviceConfig.groupId);
    snprintf(TOPIC_GROUP_COMMAND, sizeof(TOPIC_GROUP_COMMAND),
             "vending/group/%s/command", deviceConfig.groupId);
  } else {
    TOPIC_GROUP_CONFIG[0] = '\0'; // Empty if no group
    TOPIC_GROUP_COMMAND[0] = '\0';
  }
}
