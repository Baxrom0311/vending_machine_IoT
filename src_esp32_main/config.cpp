#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "display.h"
#include <WiFi.h>
#include <cstdio>

// ============================================
// GLOBAL CONFIG INSTANCE (from deviceConfig)
// ============================================
Config config;

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
  setDisplayNetworkStatus(message);
}

static void startWiFiConnect() {
  if (deviceConfig.wifi_ssid[0] == '\0') {
    DEBUG_PRINTLN("WiFi not configured!");
    printWiFiStatus("Not configured");
    wifiState = WIFI_FAILED;
    return;
  }

  DEBUG_PRINT("Connecting to WiFi: ");
  DEBUG_PRINTLN(deviceConfig.wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(deviceConfig.wifi_ssid, deviceConfig.wifi_password);

  wifiState = WIFI_CONNECTING;
  wifiStartMs = millis();
  printWiFiStatus("Connecting...");
}

void setupWiFi() { startWiFiConnect(); }

void processWiFi() {
  if (wifiState == WIFI_CONNECTED && WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi disconnected");
    printWiFiStatus("Disconnected");
    wifiState = WIFI_FAILED;
    wifiRetryMs = millis();
  }

  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi Connected!");
      DEBUG_PRINT("IP: ");
      DEBUG_PRINTLN(WiFi.localIP());
      printWiFiStatus("Connected");
      wifiState = WIFI_CONNECTED;
      return;
    }
    if (millis() - wifiStartMs > 10000) {
      DEBUG_PRINTLN("WiFi connect timeout");
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
  config.pulsesPerLiter = deviceConfig.pulsesPerLiter;
  config.tdsThreshold = deviceConfig.tdsThreshold;
  config.tdsTemperatureC = deviceConfig.tdsTemperatureC;
  config.tdsCalibrationFactor = deviceConfig.tdsCalibrationFactor;
  config.relayActiveHigh = deviceConfig.relayActiveHigh;
  config.cashPulseValue = deviceConfig.cashPulseValue;
  config.cashPulseGapMs = deviceConfig.cashPulseGapMs;
  config.paymentCheckInterval = deviceConfig.paymentCheckInterval;
  config.displayUpdateInterval = deviceConfig.displayUpdateInterval;
  config.tdsCheckInterval = deviceConfig.tdsCheckInterval;
  config.heartbeatInterval = deviceConfig.heartbeatInterval;
}

// ============================================
// CONFIG INITIALIZATION
// ============================================
void initConfig() {
  applyRuntimeConfig();
  DEBUG_PRINTLN("Config initialized from storage");
}
