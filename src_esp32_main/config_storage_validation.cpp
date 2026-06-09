#include "config_storage.h"
#include "debug.h"
#include "settings.h"
#include <Arduino.h>

static bool validRelayPhase(unsigned long onMs, unsigned long offMs) {
  if (offMs == 0) {
    return onMs == 0 || (onMs >= 100 && onMs <= 3600000);
  }
  return onMs >= 100 && onMs <= 3600000 && offMs >= 100 &&
         offMs <= 3600000;
}

// ============================================
// VALIDATE CONFIGURATION
// ============================================
void validateConfig() {
  bool changed = false;

  // Migration: apply new defaults for known old schema values.
  if (deviceConfig.configVersion < CURRENT_DEVICE_CONFIG_VERSION) {
    if (deviceConfig.cashPulseValue == 1000 && deviceConfig.cashPulseGapMs == 120) {
      deviceConfig.cashPulseValue = 500;
      deviceConfig.cashPulseGapMs = 600;
      DEBUG_PRINTLN("Config migration: cash defaults updated to 500/600");
    }
    if (deviceConfig.cashPulseValue == 500 && deviceConfig.cashPulseGapMs == 600) {
      deviceConfig.cashPulseValue = 1000;
      DEBUG_PRINTLN("Config migration: cash pulse value updated to 1000");
    }
    if (deviceConfig.pricePerLiter == 1000) {
      deviceConfig.pricePerLiter = 500;
      DEBUG_PRINTLN("Config migration: price default updated to 500 so'm/L");
    }
    if (deviceConfig.relayActiveHigh != DEFAULT_RELAY_ACTIVE_HIGH) {
      deviceConfig.relayActiveHigh = DEFAULT_RELAY_ACTIVE_HIGH;
      DEBUG_PRINTLN("Config migration: relay polarity updated to ACTIVE_LOW");
    }
    deviceConfig.configVersion = CURRENT_DEVICE_CONFIG_VERSION;
    changed = true;
  }

  if (deviceConfig.pricePerLiter < 100 || deviceConfig.pricePerLiter > 100000) {
    deviceConfig.pricePerLiter = DEFAULT_PRICE_PER_LITER;
    changed = true;
  }
  if (deviceConfig.sessionTimeout < 1000) {
    deviceConfig.sessionTimeout = DEFAULT_SESSION_TIMEOUT_MS;
    changed = true;
  }
  if (!validRelayPhase(deviceConfig.relay1OnMs, deviceConfig.relay1OffMs)) {
    deviceConfig.relay1OnMs = 0;
    deviceConfig.relay1OffMs = 0;
    changed = true;
  }
  if (!validRelayPhase(deviceConfig.relay2OnMs, deviceConfig.relay2OffMs)) {
    deviceConfig.relay2OnMs = 0;
    deviceConfig.relay2OffMs = 0;
    changed = true;
  }
  if (deviceConfig.cashPulseValue <= 0) {
    deviceConfig.cashPulseValue = DEFAULT_CASH_PULSE_VALUE;
    changed = true;
  }
  if (deviceConfig.cashPulseGapMs < 20 || deviceConfig.cashPulseGapMs > 1000) {
    deviceConfig.cashPulseGapMs = DEFAULT_CASH_PULSE_GAP_MS;
    changed = true;
  }
  if (deviceConfig.paymentCheckInterval < 200 ||
      deviceConfig.paymentCheckInterval > 600000) {
    deviceConfig.paymentCheckInterval = DEFAULT_PAYMENT_CHECK_INTERVAL_MS;
    changed = true;
  }
  if (deviceConfig.heartbeatInterval < 5000 ||
      deviceConfig.heartbeatInterval > 3600000) {
    deviceConfig.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL_MS;
    changed = true;
  }
  if (changed) {
    DEBUG_PRINTLN("Config validation corrected invalid values.");
    saveConfigToStorage();
  }
}
