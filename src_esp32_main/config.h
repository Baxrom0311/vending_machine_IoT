#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "settings.h"

// ============================================
// CONFIGURATION STRUCTURE
// ============================================
struct Config {
  int pricePerLiter = DEFAULT_PRICE_PER_LITER; // so'm
  unsigned long sessionTimeout = DEFAULT_SESSION_TIMEOUT_MS;
  bool relayActiveHigh = DEFAULT_RELAY_ACTIVE_HIGH;
  unsigned long relay1OnMs = DEFAULT_RELAY1_ON_MS;
  unsigned long relay1OffMs = DEFAULT_RELAY1_OFF_MS;
  unsigned long relay2OnMs = DEFAULT_RELAY2_ON_MS;
  unsigned long relay2OffMs = DEFAULT_RELAY2_OFF_MS;
  int cashPulseValue = DEFAULT_CASH_PULSE_VALUE;
  unsigned long cashPulseGapMs = DEFAULT_CASH_PULSE_GAP_MS;
  unsigned long paymentCheckInterval = DEFAULT_PAYMENT_CHECK_INTERVAL_MS;
  unsigned long heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL_MS;
  // Power Management removed
};

// ============================================
// GLOBAL CONFIG INSTANCE
// ============================================
extern Config config;

// ============================================
// FUNCTIONS
// ============================================
void initConfig();
void applyRuntimeConfig();

#endif
