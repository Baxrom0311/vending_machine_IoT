#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// CONFIGURATION STRUCTURE
// ============================================
struct Config {
  int pricePerLiter = 1000;                 // so'm
  unsigned long sessionTimeout = 300000;    // 5 min (ms)
  unsigned long freeWaterCooldown = 180000; // 3 min (ms)
  float freeWaterAmount = 0.2;              // 200ml (liters)
  float pulsesPerLiter = 450.0;             // Flow sensor calibration
  int tdsThreshold = 100;                   // ppm - warning only
  float tdsTemperatureC = 25.0;             // TDS compensation temp
  float tdsCalibrationFactor = 0.5;         // TDS calibration multiplier
  bool enableFreeWater = true;              // Feature flag
  bool relayActiveHigh = true;        // Relay polarity (forced Active HIGH)
  int cashPulseValue = 1000;          // so'm per pulse
  unsigned long cashPulseGapMs = 120; // gap to close pulse burst
  unsigned long paymentCheckInterval = 2000; // 2s
  unsigned long displayUpdateInterval = 100; // 100ms
  unsigned long tdsCheckInterval = 5000;     // 5s
  unsigned long heartbeatInterval = 30000;   // 30s
  // Power Management removed
};

// ============================================
// MQTT TOPICS (dynamically generated from device_id)
// ============================================
extern char TOPIC_PAYMENT_IN[64];
extern char TOPIC_STATUS_OUT[64];
extern char TOPIC_CONFIG_IN[64];
extern char TOPIC_LOG_OUT[64];
extern char TOPIC_TDS_OUT[64];
extern char TOPIC_HEARTBEAT[64];
extern char TOPIC_OTA_IN[64];
extern char TOPIC_TELEMETRY[64];
extern char TOPIC_ALERTS[64];
extern char TOPIC_DIAGNOSTICS[64];

// LOW FIX: Removed unused extern declarations (wifi_ssid, mqtt_broker, etc.)
// These are now handled within DeviceConfig struct in config_storage.h

// Fleet Management Topics
extern char TOPIC_BROADCAST_CONFIG[64];
extern char TOPIC_BROADCAST_COMMAND[64];
extern char TOPIC_GROUP_CONFIG[64];
extern char TOPIC_GROUP_COMMAND[64];

// ============================================
// GLOBAL CONFIG INSTANCE
// ============================================
extern Config config;

// ============================================
// FUNCTIONS
// ============================================
void setupWiFi();
void processWiFi();
void initConfig();
void applyRuntimeConfig();
void generateMQTTTopics(); // Generate topics from device_id

#endif
