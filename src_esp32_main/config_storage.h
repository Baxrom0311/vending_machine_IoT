#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Arduino.h>
#include <Preferences.h>

// ============================================
// DEVICE CONFIGURATION STRUCTURE
// ============================================
struct DeviceConfig {
  // Device identity
  char device_id[32];

  // Vending Settings
  int pricePerLiter;
  unsigned long sessionTimeout;
  bool relayActiveHigh;
  unsigned long relay1OnMs;
  unsigned long relay1OffMs;
  unsigned long relay2OnMs;
  unsigned long relay2OffMs;
  int cashPulseValue;
  unsigned long cashPulseGapMs;

  // Intervals
  unsigned long paymentCheckInterval;
  unsigned long heartbeatInterval;

  // Version & Flags
  int configVersion;
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

#endif
