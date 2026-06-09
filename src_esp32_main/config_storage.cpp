#include "config_storage.h"
#include "debug.h"
#include "settings.h"
#include <Preferences.h> // Ensure PlatformIO LDF picks up ESP32 Preferences
#include <cmath>
#include <cstdint>
#include <cstring>

// ============================================
// GLOBAL INSTANCES
// ============================================
Preferences preferences;
DeviceConfig deviceConfig;
static bool pendingConfigSave = false;
static unsigned long pendingConfigSaveSince = 0;
static const unsigned long CONFIG_SAVE_DEBOUNCE_MS = 2000;
static DeviceConfig lastSavedSnapshot;
static bool hasSavedSnapshot = false;

static bool nearlyEqualFloat(float a, float b) {
  return std::fabs(a - b) <= 0.0001f;
}

static bool configsEqual(const DeviceConfig &a, const DeviceConfig &b) {
  if (strncmp(a.device_id, b.device_id, sizeof(a.device_id)) != 0)
    return false;
  if (a.pricePerLiter != b.pricePerLiter)
    return false;
  if (a.sessionTimeout != b.sessionTimeout)
    return false;
  if (a.relayActiveHigh != b.relayActiveHigh)
    return false;
  if (a.relay1OnMs != b.relay1OnMs)
    return false;
  if (a.relay1OffMs != b.relay1OffMs)
    return false;
  if (a.relay2OnMs != b.relay2OnMs)
    return false;
  if (a.relay2OffMs != b.relay2OffMs)
    return false;
  if (a.cashPulseValue != b.cashPulseValue)
    return false;
  if (a.cashPulseGapMs != b.cashPulseGapMs)
    return false;
  if (a.paymentCheckInterval != b.paymentCheckInterval)
    return false;
  if (a.heartbeatInterval != b.heartbeatInterval)
    return false;
  if (a.configVersion != b.configVersion)
    return false;
  return true;
}

static void rememberSavedSnapshot() {
  lastSavedSnapshot = deviceConfig;
  hasSavedSnapshot = true;
}

// ============================================
// DEFAULT CONFIGURATION
// ============================================
void loadDefaultConfig() {
  strcpy(deviceConfig.device_id, DEFAULT_DEVICE_ID);

  // Vending Settings
  deviceConfig.pricePerLiter = DEFAULT_PRICE_PER_LITER;
  deviceConfig.sessionTimeout = DEFAULT_SESSION_TIMEOUT_MS;
  deviceConfig.relayActiveHigh = DEFAULT_RELAY_ACTIVE_HIGH;
  deviceConfig.relay1OnMs = DEFAULT_RELAY1_ON_MS;
  deviceConfig.relay1OffMs = DEFAULT_RELAY1_OFF_MS;
  deviceConfig.relay2OnMs = DEFAULT_RELAY2_ON_MS;
  deviceConfig.relay2OffMs = DEFAULT_RELAY2_OFF_MS;
  deviceConfig.cashPulseValue = DEFAULT_CASH_PULSE_VALUE;
  deviceConfig.cashPulseGapMs = DEFAULT_CASH_PULSE_GAP_MS;

  // Intervals
  deviceConfig.paymentCheckInterval = DEFAULT_PAYMENT_CHECK_INTERVAL_MS;
  deviceConfig.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL_MS;

  // Flags
  deviceConfig.configVersion = CURRENT_DEVICE_CONFIG_VERSION;
}

// ============================================
// INITIALIZE CONFIG STORAGE
// ============================================
void initConfigStorage() {
  DEBUG_PRINTLN("Initializing config storage...");

  if (!preferences.begin("ewater", true)) {
    DEBUG_PRINTLN("⚠️ NVS open failed, loading defaults");
    loadDefaultConfig();
    return;
  }
  bool hasConfig = preferences.getBool("has_config", false);
  preferences.end();

  if (!hasConfig) {
    DEBUG_PRINTLN("No saved config found. Loading defaults...");
    loadDefaultConfig();
    saveConfigToStorage();
  } else {
    DEBUG_PRINTLN("Loading saved config...");
    loadConfigFromStorage();
    validateConfig(); // Validate loaded config
  }

  DEBUG_PRINTLN("Config storage initialized.");
}

// ============================================
// LOAD CONFIG FROM STORAGE
// ============================================
void loadConfigFromStorage() {
  if (!preferences.begin("ewater", true)) { // Read-only mode
    DEBUG_PRINTLN("⚠️ NVS read open failed, using defaults");
    loadDefaultConfig();
    rememberSavedSnapshot();
    return;
  }

  String devId = preferences.getString("device_id", DEFAULT_DEVICE_ID);
  size_t n = devId.length();
  if (n >= sizeof(deviceConfig.device_id)) {
    n = sizeof(deviceConfig.device_id) - 1;
  }
  memcpy(deviceConfig.device_id, devId.c_str(), n);
  deviceConfig.device_id[n] = '\0';

  // Vending Settings
  deviceConfig.pricePerLiter =
      preferences.getInt("price", DEFAULT_PRICE_PER_LITER);
  deviceConfig.sessionTimeout =
      preferences.getULong("sess_timeout", DEFAULT_SESSION_TIMEOUT_MS);
  deviceConfig.relayActiveHigh =
      preferences.getBool("relay_active_high", DEFAULT_RELAY_ACTIVE_HIGH);
  deviceConfig.relay1OnMs =
      preferences.getULong("relay1_on", DEFAULT_RELAY1_ON_MS);
  deviceConfig.relay1OffMs =
      preferences.getULong("relay1_off", DEFAULT_RELAY1_OFF_MS);
  deviceConfig.relay2OnMs =
      preferences.getULong("relay2_on", DEFAULT_RELAY2_ON_MS);
  deviceConfig.relay2OffMs =
      preferences.getULong("relay2_off", DEFAULT_RELAY2_OFF_MS);
  deviceConfig.cashPulseValue =
      preferences.getInt("cash_pulse", DEFAULT_CASH_PULSE_VALUE);
  deviceConfig.cashPulseGapMs =
      preferences.getULong("cash_gap", DEFAULT_CASH_PULSE_GAP_MS);

  // Intervals
  deviceConfig.paymentCheckInterval = preferences.getULong(
      "pay_interval", DEFAULT_PAYMENT_CHECK_INTERVAL_MS);
  deviceConfig.heartbeatInterval = preferences.getULong(
      "hb_interval", DEFAULT_HEARTBEAT_INTERVAL_MS);

  // Meta
  deviceConfig.configVersion =
      preferences.getInt("cfg_version", CURRENT_DEVICE_CONFIG_VERSION);

  preferences.end();

  rememberSavedSnapshot();
  DEBUG_PRINTLN("Config loaded from storage.");
}

// ============================================
// SAVE CONFIG TO STORAGE
// ============================================
void saveConfigToStorage() {
  if (hasSavedSnapshot && configsEqual(deviceConfig, lastSavedSnapshot)) {
    pendingConfigSave = false;
    DEBUG_PRINTLN("Config unchanged, skip flash write.");
    return;
  }

  if (!preferences.begin("ewater", false)) { // Read/Write mode
    DEBUG_PRINTLN("⚠️ NVS write open failed. Config not saved.");
    return;
  }
  bool saveOk = true;

  auto putStrChecked = [&](const char *key, const char *value) {
    size_t written = preferences.putString(key, value);
    if (value && value[0] != '\0' && written == 0) {
      saveOk = false;
    }
  };

  auto putFixedChecked = [&](size_t written, size_t expected) {
    if (written != expected) {
      saveOk = false;
    }
  };

  putStrChecked("device_id", deviceConfig.device_id);

  // Vending Settings
  putFixedChecked(preferences.putInt("price", deviceConfig.pricePerLiter),
                  sizeof(int32_t));
  putFixedChecked(
      preferences.putULong("sess_timeout", deviceConfig.sessionTimeout),
      sizeof(uint32_t));
  putFixedChecked(
      preferences.putBool("relay_active_high", deviceConfig.relayActiveHigh),
      sizeof(uint8_t));
  putFixedChecked(preferences.putULong("relay1_on", deviceConfig.relay1OnMs),
                  sizeof(uint32_t));
  putFixedChecked(preferences.putULong("relay1_off", deviceConfig.relay1OffMs),
                  sizeof(uint32_t));
  putFixedChecked(preferences.putULong("relay2_on", deviceConfig.relay2OnMs),
                  sizeof(uint32_t));
  putFixedChecked(preferences.putULong("relay2_off", deviceConfig.relay2OffMs),
                  sizeof(uint32_t));
  putFixedChecked(preferences.putInt("cash_pulse", deviceConfig.cashPulseValue),
                  sizeof(int32_t));
  putFixedChecked(preferences.putULong("cash_gap", deviceConfig.cashPulseGapMs),
                  sizeof(uint32_t));

  // Intervals
  putFixedChecked(
      preferences.putULong("pay_interval", deviceConfig.paymentCheckInterval),
      sizeof(uint32_t));
  putFixedChecked(
      preferences.putULong("hb_interval", deviceConfig.heartbeatInterval),
      sizeof(uint32_t));

  // Meta
  putFixedChecked(preferences.putInt("cfg_version", deviceConfig.configVersion),
                  sizeof(int32_t));
  putFixedChecked(preferences.putBool("has_config", true), sizeof(uint8_t));

  preferences.end();

  if (!saveOk) {
    DEBUG_PRINTLN("⚠️ Config save completed with write mismatches.");
  }

  rememberSavedSnapshot();
  pendingConfigSave = false;

  DEBUG_PRINTLN("Config saved to storage.");
}

void scheduleConfigSave() {
  pendingConfigSave = true;
  pendingConfigSaveSince = millis();
}

void processConfigSave() {
  if (!pendingConfigSave) {
    return;
  }
  if (millis() - pendingConfigSaveSince < CONFIG_SAVE_DEBOUNCE_MS) {
    return;
  }
  saveConfigToStorage();
}
