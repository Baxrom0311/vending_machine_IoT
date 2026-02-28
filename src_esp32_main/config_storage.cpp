#include "config_storage.h"
#include "debug.h"
#include <Preferences.h> // Ensure PlatformIO LDF picks up ESP32 Preferences
#include <cstdint>
#include <cmath>
#include <cstring>

static void copyToBuffer(char *dst, size_t dstSize, const String &src) {
  size_t n = src.length();
  if (n >= dstSize) {
    n = dstSize - 1;
  }
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

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
  if (strncmp(a.wifi_ssid, b.wifi_ssid, sizeof(a.wifi_ssid)) != 0)
    return false;
  if (strncmp(a.wifi_password, b.wifi_password, sizeof(a.wifi_password)) != 0)
    return false;
  if (strncmp(a.mqtt_broker, b.mqtt_broker, sizeof(a.mqtt_broker)) != 0)
    return false;
  if (a.mqtt_port != b.mqtt_port)
    return false;
  if (strncmp(a.mqtt_username, b.mqtt_username, sizeof(a.mqtt_username)) != 0)
    return false;
  if (strncmp(a.mqtt_password, b.mqtt_password, sizeof(a.mqtt_password)) != 0)
    return false;
  if (strncmp(a.device_id, b.device_id, sizeof(a.device_id)) != 0)
    return false;
  if (strncmp(a.api_secret, b.api_secret, sizeof(a.api_secret)) != 0)
    return false;
  if (a.requireSignedMessages != b.requireSignedMessages)
    return false;
  if (a.allowRemoteNetworkConfig != b.allowRemoteNetworkConfig)
    return false;
  if (a.pricePerLiter != b.pricePerLiter)
    return false;
  if (a.sessionTimeout != b.sessionTimeout)
    return false;
  if (!nearlyEqualFloat(a.pulsesPerLiter, b.pulsesPerLiter))
    return false;
  if (a.tdsThreshold != b.tdsThreshold)
    return false;
  if (!nearlyEqualFloat(a.tdsTemperatureC, b.tdsTemperatureC))
    return false;
  if (!nearlyEqualFloat(a.tdsCalibrationFactor, b.tdsCalibrationFactor))
    return false;
  if (a.relayActiveHigh != b.relayActiveHigh)
    return false;
  if (a.cashPulseValue != b.cashPulseValue)
    return false;
  if (a.cashPulseGapMs != b.cashPulseGapMs)
    return false;
  if (a.paymentCheckInterval != b.paymentCheckInterval)
    return false;
  if (a.displayUpdateInterval != b.displayUpdateInterval)
    return false;
  if (a.tdsCheckInterval != b.tdsCheckInterval)
    return false;
  if (a.heartbeatInterval != b.heartbeatInterval)
    return false;
  if (a.enablePowerSave != b.enablePowerSave)
    return false;
  if (a.deepSleepStartHour != b.deepSleepStartHour)
    return false;
  if (a.deepSleepEndHour != b.deepSleepEndHour)
    return false;
  if (strncmp(a.groupId, b.groupId, sizeof(a.groupId)) != 0)
    return false;
  if (a.configVersion != b.configVersion)
    return false;
  if (a.configured != b.configured)
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
  // WiFi (empty by default - must be provisioned explicitly)
  deviceConfig.wifi_ssid[0] = '\0';
  deviceConfig.wifi_password[0] = '\0';

  // MQTT
  deviceConfig.mqtt_broker[0] = '\0';
  deviceConfig.mqtt_port = 1883;
  strcpy(deviceConfig.mqtt_username, "");
  strcpy(deviceConfig.mqtt_password, "");
  strcpy(deviceConfig.device_id, "VendingMachine_001");
  strcpy(deviceConfig.api_secret, "");
  deviceConfig.requireSignedMessages = false;
  deviceConfig.allowRemoteNetworkConfig = true;

  // Vending Settings
  deviceConfig.pricePerLiter = 500;
  deviceConfig.sessionTimeout = 300000; // 5 min
  deviceConfig.pulsesPerLiter = 450.0;
  deviceConfig.tdsThreshold = 100;
  deviceConfig.tdsTemperatureC = 25.0;
  deviceConfig.tdsCalibrationFactor = 0.5;
  deviceConfig.relayActiveHigh = true; // Relay polarity (Active HIGH for modules)
  deviceConfig.cashPulseValue = 500;
  deviceConfig.cashPulseGapMs = 600;

  // Intervals
  deviceConfig.paymentCheckInterval = 2000;
  deviceConfig.displayUpdateInterval = 100;
  deviceConfig.tdsCheckInterval = 5000;
  deviceConfig.heartbeatInterval = 30000;

  // Power Management
  deviceConfig.enablePowerSave = false;
  deviceConfig.deepSleepStartHour = 1; // 01:00
  deviceConfig.deepSleepEndHour = 6;   // 06:00

  // Fleet Management
  strcpy(deviceConfig.groupId, ""); // Empty by default (no group)

  // Flags
  deviceConfig.configVersion = 3;
  deviceConfig.configured = false;
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

  // WiFi
  String ssid = preferences.getString("wifi_ssid", "");
  String pass = preferences.getString("wifi_pass", "");
  copyToBuffer(deviceConfig.wifi_ssid, sizeof(deviceConfig.wifi_ssid), ssid);
  copyToBuffer(deviceConfig.wifi_password, sizeof(deviceConfig.wifi_password),
               pass);

  // MQTT
  String broker = preferences.getString("mqtt_broker", "");
  String username = preferences.getString("mqtt_user", "");
  String password = preferences.getString("mqtt_pass", "");
  String devId = preferences.getString("device_id", "VendingMachine_001");

  copyToBuffer(deviceConfig.mqtt_broker, sizeof(deviceConfig.mqtt_broker),
               broker);
  deviceConfig.mqtt_port = preferences.getInt("mqtt_port", 1883);
  copyToBuffer(deviceConfig.mqtt_username, sizeof(deviceConfig.mqtt_username),
               username);
  copyToBuffer(deviceConfig.mqtt_password, sizeof(deviceConfig.mqtt_password),
               password);
  copyToBuffer(deviceConfig.device_id, sizeof(deviceConfig.device_id), devId);
  String apiSecret = preferences.getString("api_secret", "");
  copyToBuffer(deviceConfig.api_secret, sizeof(deviceConfig.api_secret),
               apiSecret);
  deviceConfig.requireSignedMessages = preferences.getBool("req_signed", false);
  deviceConfig.allowRemoteNetworkConfig =
      preferences.getBool("allow_netcfg", true);

  // Vending Settings
  deviceConfig.pricePerLiter = preferences.getInt("price", 500);
  deviceConfig.sessionTimeout = preferences.getULong("sess_timeout", 300000);
  deviceConfig.pulsesPerLiter = preferences.getFloat("pulses", 450.0);
  deviceConfig.tdsThreshold = preferences.getInt("tds_thresh", 100);
  deviceConfig.tdsTemperatureC = preferences.getFloat("tds_temp", 25.0);
  deviceConfig.tdsCalibrationFactor = preferences.getFloat("tds_calib", 0.5);
  // Hardware policy: relay is fixed Active-HIGH.
  deviceConfig.relayActiveHigh = true;
  deviceConfig.cashPulseValue = preferences.getInt("cash_pulse", 500);
  deviceConfig.cashPulseGapMs = preferences.getULong("cash_gap", 600);

  // Intervals
  deviceConfig.paymentCheckInterval =
      preferences.getULong("pay_interval", 2000);
  deviceConfig.displayUpdateInterval =
      preferences.getULong("disp_interval", 100);
  deviceConfig.tdsCheckInterval = preferences.getULong("tds_interval", 5000);
  deviceConfig.heartbeatInterval = preferences.getULong("hb_interval", 30000);

  // Power Management
  deviceConfig.enablePowerSave = preferences.getBool("enable_ps", true);
  deviceConfig.deepSleepStartHour = preferences.getInt("sleep_start", 1);
  deviceConfig.deepSleepEndHour = preferences.getInt("sleep_end", 6);

  // Fleet Management
  String groupIdStr = preferences.getString("group_id", "");
  strncpy(deviceConfig.groupId, groupIdStr.c_str(),
          sizeof(deviceConfig.groupId) - 1);
  deviceConfig.groupId[sizeof(deviceConfig.groupId) - 1] = '\0';

  // Meta
  deviceConfig.configVersion = preferences.getInt("cfg_version", 1);
  deviceConfig.configured = preferences.getBool("configured", false);

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

  // WiFi
  putStrChecked("wifi_ssid", deviceConfig.wifi_ssid);
  putStrChecked("wifi_pass", deviceConfig.wifi_password);

  // MQTT
  putStrChecked("mqtt_broker", deviceConfig.mqtt_broker);
  putFixedChecked(preferences.putInt("mqtt_port", deviceConfig.mqtt_port),
                  sizeof(int32_t));
  putStrChecked("mqtt_user", deviceConfig.mqtt_username);
  putStrChecked("mqtt_pass", deviceConfig.mqtt_password);
  putStrChecked("device_id", deviceConfig.device_id);
  putStrChecked("api_secret", deviceConfig.api_secret);
  putFixedChecked(
      preferences.putBool("req_signed", deviceConfig.requireSignedMessages),
      sizeof(uint8_t));
  putFixedChecked(
      preferences.putBool("allow_netcfg", deviceConfig.allowRemoteNetworkConfig),
      sizeof(uint8_t));

  // Vending Settings
  putFixedChecked(preferences.putInt("price", deviceConfig.pricePerLiter),
                  sizeof(int32_t));
  putFixedChecked(
      preferences.putULong("sess_timeout", deviceConfig.sessionTimeout),
      sizeof(uint32_t));
  putFixedChecked(preferences.putFloat("pulses", deviceConfig.pulsesPerLiter),
                  sizeof(float));
  putFixedChecked(preferences.putInt("tds_thresh", deviceConfig.tdsThreshold),
                  sizeof(int32_t));
  putFixedChecked(preferences.putFloat("tds_temp", deviceConfig.tdsTemperatureC),
                  sizeof(float));
  putFixedChecked(
      preferences.putFloat("tds_calib", deviceConfig.tdsCalibrationFactor),
      sizeof(float));
  putFixedChecked(
      preferences.putBool("relay_active_high", deviceConfig.relayActiveHigh),
      sizeof(uint8_t));
  putFixedChecked(preferences.putInt("cash_pulse", deviceConfig.cashPulseValue),
                  sizeof(int32_t));
  putFixedChecked(preferences.putULong("cash_gap", deviceConfig.cashPulseGapMs),
                  sizeof(uint32_t));

  // Intervals
  putFixedChecked(
      preferences.putULong("pay_interval", deviceConfig.paymentCheckInterval),
      sizeof(uint32_t));
  putFixedChecked(
      preferences.putULong("disp_interval", deviceConfig.displayUpdateInterval),
      sizeof(uint32_t));
  putFixedChecked(
      preferences.putULong("tds_interval", deviceConfig.tdsCheckInterval),
      sizeof(uint32_t));
  putFixedChecked(
      preferences.putULong("hb_interval", deviceConfig.heartbeatInterval),
      sizeof(uint32_t));

  // Power Management
  putFixedChecked(preferences.putBool("enable_ps", deviceConfig.enablePowerSave),
                  sizeof(uint8_t));
  putFixedChecked(preferences.putInt("sleep_start", deviceConfig.deepSleepStartHour),
                  sizeof(int32_t));
  putFixedChecked(preferences.putInt("sleep_end", deviceConfig.deepSleepEndHour),
                  sizeof(int32_t));

  // Fleet Management
  putStrChecked("group_id", deviceConfig.groupId);

  // Meta
  putFixedChecked(preferences.putInt("cfg_version", deviceConfig.configVersion),
                  sizeof(int32_t));
  putFixedChecked(preferences.putBool("configured", deviceConfig.configured),
                  sizeof(uint8_t));
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

// ============================================
// PRINT CURRENT CONFIG
// ============================================
void printCurrentConfig() {
  Serial.println("\n========== CURRENT CONFIGURATION ==========");
  Serial.println("[WiFi]");
  Serial.print("  SSID: ");
  Serial.println(deviceConfig.wifi_ssid[0] ? deviceConfig.wifi_ssid
                                           : "(not set)");
  Serial.print("  Password: ");
  Serial.println(deviceConfig.wifi_password[0] ? "********" : "(not set)");

  Serial.println("\n[MQTT]");
  Serial.print("  Broker: ");
  Serial.println(deviceConfig.mqtt_broker);
  Serial.print("  Port: ");
  Serial.println(deviceConfig.mqtt_port);
  Serial.print("  Device ID: ");
  Serial.println(deviceConfig.device_id);
  Serial.print("  Username: ");
  Serial.println(deviceConfig.mqtt_username[0] ? deviceConfig.mqtt_username
                                               : "(not set)");
  Serial.print("  API Secret: ");
  Serial.println(deviceConfig.api_secret[0] ? "********" : "(not set)");
  Serial.print("  Require Signed: ");
  Serial.println(deviceConfig.requireSignedMessages ? "YES" : "NO");
  Serial.print("  Remote Network Config: ");
  Serial.println(deviceConfig.allowRemoteNetworkConfig ? "Allowed"
                                                       : "Disabled");
  Serial.print("  Group ID: ");
  Serial.println(deviceConfig.groupId[0] ? deviceConfig.groupId : "(not set)");

  Serial.println("\n[Vending]");
  Serial.print("  Price per Liter: ");
  Serial.print(deviceConfig.pricePerLiter);
  Serial.println(" so'm");
  Serial.print("  Session Timeout: ");
  Serial.print(deviceConfig.sessionTimeout / 1000);
  Serial.println(" sec");
  Serial.print("  Pulses per Liter: ");
  Serial.println(deviceConfig.pulsesPerLiter, 2);
  Serial.print("  TDS Threshold: ");
  Serial.print(deviceConfig.tdsThreshold);
  Serial.println(" ppm");
  Serial.print("  TDS Temperature: ");
  Serial.print(deviceConfig.tdsTemperatureC, 1);
  Serial.println(" C");
  Serial.print("  TDS Calibration: ");
  Serial.println(deviceConfig.tdsCalibrationFactor, 3);
  Serial.print("  Relay Active High: ");
  Serial.println(deviceConfig.relayActiveHigh ? "YES" : "NO");
  Serial.print("  Cash Pulse Value: ");
  Serial.print(deviceConfig.cashPulseValue);
  Serial.println(" so'm");
  Serial.print("  Cash Pulse Gap: ");
  Serial.print(deviceConfig.cashPulseGapMs);
  Serial.println(" ms");

  Serial.print("  Payment Interval: ");
  Serial.print(deviceConfig.paymentCheckInterval);
  Serial.println(" ms");
  Serial.print("  Display Interval: ");
  Serial.print(deviceConfig.displayUpdateInterval);
  Serial.println(" ms");
  Serial.print("  TDS Interval: ");
  Serial.print(deviceConfig.tdsCheckInterval);
  Serial.println(" ms");
  Serial.print("  Heartbeat Interval: ");
  Serial.print(deviceConfig.heartbeatInterval);
  Serial.println(" ms");

  Serial.println("\n[Power]");
  Serial.print("  Enable Power Save: ");
  Serial.println(deviceConfig.enablePowerSave ? "YES" : "NO");
  Serial.print("  Deep Sleep Window: ");
  Serial.print(deviceConfig.deepSleepStartHour);
  Serial.print(":00 - ");
  Serial.print(deviceConfig.deepSleepEndHour);
  Serial.println(":00");

  Serial.println("\n[Status]");
  Serial.print("  Configured: ");
  Serial.println(deviceConfig.configured ? "YES" : "NO");
  Serial.print("  Config Version: ");
  Serial.println(deviceConfig.configVersion);
  Serial.println("==========================================\n");
}

// ============================================
// CHECK IF CONFIGURED
// ============================================
bool isConfigured() {
  return deviceConfig.configured && deviceConfig.wifi_ssid[0] != '\0' &&
         deviceConfig.mqtt_broker[0] != '\0';
}
