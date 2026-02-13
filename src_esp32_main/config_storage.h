#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Arduino.h>
#include <Preferences.h>

// ============================================
// DEVICE CONFIGURATION STRUCTURE
// ============================================
struct DeviceConfig {
  // WiFi Settings
  char wifi_ssid[32];
  char wifi_password[64];

  // MQTT Settings
  char mqtt_broker[128];
  int mqtt_port;
  char mqtt_username[32];
  char mqtt_password[64];
  char device_id[32];
  char api_secret[64];
  bool requireSignedMessages;
  bool allowRemoteNetworkConfig;

  // Vending Settings
  int pricePerLiter;
  unsigned long sessionTimeout;
  unsigned long freeWaterCooldown;
  float freeWaterAmount;
  float pulsesPerLiter;
  int tdsThreshold;
  float tdsTemperatureC;
  float tdsCalibrationFactor;
  bool enableFreeWater;
  bool relayActiveHigh;
  int cashPulseValue;
  unsigned long cashPulseGapMs;

  // Intervals
  unsigned long paymentCheckInterval;
  unsigned long displayUpdateInterval;
  unsigned long tdsCheckInterval;
  unsigned long heartbeatInterval;

  // Power Management
  bool enablePowerSave;
  int deepSleepStartHour;
  int deepSleepEndHour;

  // Fleet Management
  char groupId[32]; // Group ID for fleet management

  // Version & Flags
  int configVersion;
  bool configured;
};

// ============================================
// GLOBAL INSTANCES
// ============================================
extern Preferences preferences;
extern DeviceConfig deviceConfig;

// ============================================
// FUNCTIONS
// ============================================
void initConfigStorage();
void loadConfigFromStorage();
void saveConfigToStorage();
void scheduleConfigSave();
void processConfigSave();
void loadDefaultConfig();
void validateConfig(); // Added validation Function
void printCurrentConfig();
bool isConfigured();

#endif
