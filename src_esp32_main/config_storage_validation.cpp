#include "config_storage.h"
#include <Arduino.h>

// ============================================
// VALIDATE CONFIGURATION
// ============================================
void validateConfig() {
  bool changed = false;

  if (deviceConfig.pricePerLiter < 0) {
    deviceConfig.pricePerLiter = 0;
    changed = true;
  }
  if (deviceConfig.sessionTimeout < 1000) {
    deviceConfig.sessionTimeout = 300000; // Reset to 5 min if too small
    changed = true;
  }
  if (deviceConfig.mqtt_port <= 0) {
    deviceConfig.mqtt_port = 1883;
    changed = true;
  }
  if (deviceConfig.freeWaterAmount < 0) {
    deviceConfig.freeWaterAmount = 0;
    changed = true;
  }
  if (deviceConfig.tdsCalibrationFactor <= 0.01 ||
      deviceConfig.tdsCalibrationFactor > 10.0) {
    deviceConfig.tdsCalibrationFactor = 0.5; // Reset to reasonable default
    changed = true;
  }
  if (deviceConfig.cashPulseValue <= 0) {
    deviceConfig.cashPulseValue = 1000;
    changed = true;
  }
  if (!deviceConfig.relayActiveHigh) {
    deviceConfig.relayActiveHigh = true;
    changed = true;
  }

  if (changed) {
    Serial.println("Config validation corrected invalid values.");
    saveConfigToStorage();
  }
}
