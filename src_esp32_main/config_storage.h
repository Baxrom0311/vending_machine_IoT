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

  // Device identity
  char device_id[32];

  // Vending Settings
  int pricePerLiter;
  unsigned long sessionTimeout;
  float pulsesPerLiter;
  int tdsThreshold;
  float tdsTemperatureC;
  float tdsCalibrationFactor;
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
