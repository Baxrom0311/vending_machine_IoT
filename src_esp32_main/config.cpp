#include "config.h"
#include "config_storage.h"
#include "debug.h"

// ============================================
// GLOBAL CONFIG INSTANCE (from deviceConfig)
// ============================================
Config config;

// ============================================
// CONFIG APPLY (Runtime)
// ============================================
void applyRuntimeConfig() {
  config.pricePerLiter = deviceConfig.pricePerLiter;
  config.sessionTimeout = deviceConfig.sessionTimeout;
  config.relayActiveHigh = deviceConfig.relayActiveHigh;
  config.relay1OnMs = deviceConfig.relay1OnMs;
  config.relay1OffMs = deviceConfig.relay1OffMs;
  config.relay2OnMs = deviceConfig.relay2OnMs;
  config.relay2OffMs = deviceConfig.relay2OffMs;
  config.cashPulseValue = deviceConfig.cashPulseValue;
  config.cashPulseGapMs = deviceConfig.cashPulseGapMs;
  config.paymentCheckInterval = deviceConfig.paymentCheckInterval;
  config.heartbeatInterval = deviceConfig.heartbeatInterval;
}

// ============================================
// CONFIG INITIALIZATION
// ============================================
void initConfig() {
  applyRuntimeConfig();
  DEBUG_PRINTLN("Config initialized from storage");
}
