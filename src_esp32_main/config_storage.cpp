#include "config_storage.h"
#include <Preferences.h> // Ensure PlatformIO LDF picks up ESP32 Preferences
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

// ============================================
// DEFAULT CONFIGURATION
// ============================================
void loadDefaultConfig() {
  // WiFi (empty by default - will be configured via serial/app)
  strcpy(deviceConfig.wifi_ssid, "");
  strcpy(deviceConfig.wifi_password, "");

  // MQTT
  strcpy(deviceConfig.mqtt_broker,
         "ec2-3-72-68-85.eu-central-1.compute.amazonaws.com");
  deviceConfig.mqtt_port = 1883;
  strcpy(deviceConfig.mqtt_username, "");
  strcpy(deviceConfig.mqtt_password, "");
  strcpy(deviceConfig.device_id, "VendingMachine_001");
  strcpy(deviceConfig.api_secret, "");
  deviceConfig.requireSignedMessages = false;
  deviceConfig.allowRemoteNetworkConfig = true;

  // Vending Settings
  deviceConfig.pricePerLiter = 1000;
  deviceConfig.sessionTimeout = 300000;    // 5 min
  deviceConfig.freeWaterCooldown = 180000; // 3 min
  deviceConfig.freeWaterAmount = 0.2;      // 200ml
  deviceConfig.pulsesPerLiter = 450.0;
  deviceConfig.tdsThreshold = 100;
  deviceConfig.tdsTemperatureC = 25.0;
  deviceConfig.tdsCalibrationFactor = 0.5;
  deviceConfig.enableFreeWater = true;
  deviceConfig.relayActiveHigh =
      true; // Relay polarity (Active HIGH for modules)
  deviceConfig.cashPulseValue = 1000;
  deviceConfig.cashPulseGapMs = 120;

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
  deviceConfig.configVersion = 1;
  deviceConfig.configured = false;
}

// ============================================
// INITIALIZE CONFIG STORAGE
// ============================================
void initConfigStorage() {
  Serial.println("Initializing config storage...");

  preferences.begin("ewater", true); // Read-only mode
  bool hasConfig = preferences.getBool("has_config", false);
  preferences.end();

  if (!hasConfig) {
    Serial.println("No saved config found. Loading defaults...");
    loadDefaultConfig();
    saveConfigToStorage();
  } else {
    Serial.println("Loading saved config...");
    loadConfigFromStorage();
    validateConfig(); // Validate loaded config
  }

  Serial.println("Config storage initialized.");
}

// ============================================
// LOAD CONFIG FROM STORAGE
// ============================================
void loadConfigFromStorage() {
  preferences.begin("ewater", true); // Read-only mode

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
  deviceConfig.pricePerLiter = preferences.getInt("price", 1000);
  deviceConfig.sessionTimeout = preferences.getULong("sess_timeout", 300000);
  deviceConfig.freeWaterCooldown =
      preferences.getULong("free_cooldown", 180000);
  deviceConfig.freeWaterAmount = preferences.getFloat("free_amount", 0.2);
  deviceConfig.pulsesPerLiter = preferences.getFloat("pulses", 450.0);
  deviceConfig.tdsThreshold = preferences.getInt("tds_thresh", 100);
  deviceConfig.tdsTemperatureC = preferences.getFloat("tds_temp", 25.0);
  deviceConfig.tdsCalibrationFactor = preferences.getFloat("tds_calib", 0.5);
  deviceConfig.enableFreeWater = preferences.getBool("enable_free", true);
  // Hardware policy: relay is fixed Active-HIGH.
  deviceConfig.relayActiveHigh = true;
  deviceConfig.cashPulseValue = preferences.getInt("cash_pulse", 1000);
  deviceConfig.cashPulseGapMs = preferences.getULong("cash_gap", 120);

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

  Serial.println("Config loaded from storage.");
}

// ============================================
// SAVE CONFIG TO STORAGE
// ============================================
void saveConfigToStorage() {
  preferences.begin("ewater", false); // Read/Write mode

  // WiFi
  preferences.putString("wifi_ssid", deviceConfig.wifi_ssid);
  preferences.putString("wifi_pass", deviceConfig.wifi_password);

  // MQTT
  preferences.putString("mqtt_broker", deviceConfig.mqtt_broker);
  preferences.putInt("mqtt_port", deviceConfig.mqtt_port);
  preferences.putString("mqtt_user", deviceConfig.mqtt_username);
  preferences.putString("mqtt_pass", deviceConfig.mqtt_password);
  preferences.putString("device_id", deviceConfig.device_id);
  preferences.putString("api_secret", deviceConfig.api_secret);
  preferences.putBool("req_signed", deviceConfig.requireSignedMessages);
  preferences.putBool("allow_netcfg", deviceConfig.allowRemoteNetworkConfig);

  // Vending Settings
  preferences.putInt("price", deviceConfig.pricePerLiter);
  preferences.putULong("sess_timeout", deviceConfig.sessionTimeout);
  preferences.putULong("free_cooldown", deviceConfig.freeWaterCooldown);
  preferences.putFloat("free_amount", deviceConfig.freeWaterAmount);
  preferences.putFloat("pulses", deviceConfig.pulsesPerLiter);
  preferences.putInt("tds_thresh", deviceConfig.tdsThreshold);
  preferences.putFloat("tds_temp", deviceConfig.tdsTemperatureC);
  preferences.putFloat("tds_calib", deviceConfig.tdsCalibrationFactor);
  preferences.putBool("enable_free", deviceConfig.enableFreeWater);
  preferences.putBool("relay_active_high", deviceConfig.relayActiveHigh);
  preferences.putInt("cash_pulse", deviceConfig.cashPulseValue);
  preferences.putULong("cash_gap", deviceConfig.cashPulseGapMs);

  // Intervals
  preferences.putULong("pay_interval", deviceConfig.paymentCheckInterval);
  preferences.putULong("disp_interval", deviceConfig.displayUpdateInterval);
  preferences.putULong("tds_interval", deviceConfig.tdsCheckInterval);
  preferences.putULong("hb_interval", deviceConfig.heartbeatInterval);

  // Power Management
  preferences.putBool("enable_ps", deviceConfig.enablePowerSave);
  preferences.putInt("sleep_start", deviceConfig.deepSleepStartHour);
  preferences.putInt("sleep_end", deviceConfig.deepSleepEndHour);

  // Fleet Management
  preferences.putString("group_id", deviceConfig.groupId);

  // Meta
  preferences.putInt("cfg_version", deviceConfig.configVersion);
  preferences.putBool("configured", deviceConfig.configured);
  preferences.putBool("has_config", true);

  preferences.end();

  pendingConfigSave = false;

  Serial.println("Config saved to storage.");
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
  Serial.print("  Free Water Cooldown: ");
  Serial.print(deviceConfig.freeWaterCooldown / 1000);
  Serial.println(" sec");
  Serial.print("  Free Water Amount: ");
  Serial.print(deviceConfig.freeWaterAmount * 1000.0f, 0);
  Serial.println(" ml");
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
  Serial.print("  Free Water: ");
  Serial.println(deviceConfig.enableFreeWater ? "Enabled" : "Disabled");
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
