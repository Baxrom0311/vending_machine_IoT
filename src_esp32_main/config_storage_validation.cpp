#include "config_storage.h"
#include "debug.h"
#include <Arduino.h>

static const int CURRENT_CONFIG_VERSION = 3;

// ============================================
// VALIDATE CONFIGURATION
// ============================================
void validateConfig() {
  bool changed = false;

  // Migration: apply new defaults for known old schema values.
  if (deviceConfig.configVersion < CURRENT_CONFIG_VERSION) {
    if (deviceConfig.cashPulseValue == 1000 && deviceConfig.cashPulseGapMs == 120) {
      deviceConfig.cashPulseValue = 500;
      deviceConfig.cashPulseGapMs = 600;
      DEBUG_PRINTLN("Config migration: cash defaults updated to 500/600");
    }
    if (deviceConfig.pricePerLiter == 1000) {
      deviceConfig.pricePerLiter = 500;
      DEBUG_PRINTLN("Config migration: price default updated to 500 so'm/L");
    }
    deviceConfig.configVersion = CURRENT_CONFIG_VERSION;
    changed = true;
  }

  if (deviceConfig.pricePerLiter < 100 || deviceConfig.pricePerLiter > 100000) {
    deviceConfig.pricePerLiter = 500;
    changed = true;
  }
  if (deviceConfig.sessionTimeout < 1000) {
    deviceConfig.sessionTimeout = 300000; // Reset to 5 min if too small
    changed = true;
  }
  if (deviceConfig.pulsesPerLiter < 1.0f ||
      deviceConfig.pulsesPerLiter > 5000.0f) {
    deviceConfig.pulsesPerLiter = 450.0f;
    changed = true;
  }
  if (deviceConfig.mqtt_port <= 0) {
    deviceConfig.mqtt_port = 1883;
    changed = true;
  }
  if (deviceConfig.tdsCalibrationFactor <= 0.01 ||
      deviceConfig.tdsCalibrationFactor > 10.0) {
    deviceConfig.tdsCalibrationFactor = 0.5; // Reset to reasonable default
    changed = true;
  }
  if (deviceConfig.cashPulseValue <= 0) {
    deviceConfig.cashPulseValue = 500;
    changed = true;
  }
  if (deviceConfig.cashPulseGapMs < 20 || deviceConfig.cashPulseGapMs > 1000) {
    deviceConfig.cashPulseGapMs = 600;
    changed = true;
  }
  if (deviceConfig.displayUpdateInterval < 100 ||
      deviceConfig.displayUpdateInterval > 10000) {
    deviceConfig.displayUpdateInterval = 100;
    changed = true;
  }
  if (deviceConfig.paymentCheckInterval < 200 ||
      deviceConfig.paymentCheckInterval > 600000) {
    deviceConfig.paymentCheckInterval = 2000;
    changed = true;
  }
  if (deviceConfig.tdsCheckInterval < 1000 ||
      deviceConfig.tdsCheckInterval > 600000) {
    deviceConfig.tdsCheckInterval = 5000;
    changed = true;
  }
  if (deviceConfig.heartbeatInterval < 5000 ||
      deviceConfig.heartbeatInterval > 3600000) {
    deviceConfig.heartbeatInterval = 30000;
    changed = true;
  }
  if (changed) {
    DEBUG_PRINTLN("Config validation corrected invalid values.");
    saveConfigToStorage();
  }
}
