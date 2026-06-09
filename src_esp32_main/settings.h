#ifndef SETTINGS_H
#define SETTINGS_H

static constexpr const char *DEFAULT_DEVICE_ID = "VendingMachine_001";
static constexpr int DEFAULT_PRICE_PER_LITER = 10000;
static constexpr unsigned long DEFAULT_SESSION_TIMEOUT_MS = 180000UL;
static constexpr bool DEFAULT_RELAY_ACTIVE_HIGH = false;
static constexpr unsigned long DEFAULT_RELAY1_ON_MS = 0UL;
static constexpr unsigned long DEFAULT_RELAY1_OFF_MS = 0UL;
static constexpr unsigned long DEFAULT_RELAY2_ON_MS = 0UL;
static constexpr unsigned long DEFAULT_RELAY2_OFF_MS = 0UL;
static constexpr int DEFAULT_CASH_PULSE_VALUE = 1000;
static constexpr unsigned long DEFAULT_CASH_PULSE_GAP_MS = 600UL;
static constexpr unsigned long DEFAULT_PAYMENT_CHECK_INTERVAL_MS = 2000UL;
static constexpr unsigned long DEFAULT_HEARTBEAT_INTERVAL_MS = 30000UL;
static constexpr int CURRENT_DEVICE_CONFIG_VERSION = 6;

#endif
