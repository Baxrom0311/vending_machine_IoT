#include "serial_config.h"
#include "config.h"
#include "config_storage.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include "sensors.h"
#include "state_machine.h"
#include <WiFi.h>
#include <cstring>

static void copyToBuffer(char *dst, size_t dstSize, const String &src) {
  size_t n = src.length();
  if (n >= dstSize) {
    n = dstSize - 1;
  }
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

static unsigned long normalizeSecondsOrMs(unsigned long value) {
  if (value == 0) {
    return value;
  }
  if (value <= 3600UL) {
    return value * 1000UL;
  }
  return value;
}

static float normalizeFreeWaterAmount(float value) {
  // Accept liters (<= 5.0) or ml (> 5.0)
  if (value <= 0.0f) {
    return 0.0f;
  }
  if (value > 5.0f) {
    return value / 1000.0f;
  }
  return value;
}

// ============================================
// INITIALIZATION
// ============================================
void initSerialConfig() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   eWater Vending Machine v2.0         ║");
  Serial.println("║   Serial Configuration Interface       ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("\nType 'HELP' for available commands\n");
}

// ============================================
// HANDLE SERIAL INPUT
// ============================================
void handleSerialConfig() {
  static String buffer;
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        buffer.trim();
        if (buffer.length() > 0) {
          processCommand(buffer);
        }
        buffer = "";
      }
      continue;
    }

    if (buffer.length() < 128) {
      buffer += c;
    }
  }
}

// ============================================
// PROCESS COMMAND
// ============================================
void processCommand(String cmd) {
  String cmdUpper = cmd;
  cmdUpper.toUpperCase();

  Serial.print("> ");
  Serial.println(cmd);

  // GET_CONFIG
  if (cmdUpper == "GET_CONFIG") {
    printCurrentConfig();
    Serial.println("OK");
  }

  // SET_WIFI:ssid:password
  else if (cmdUpper.startsWith("SET_WIFI:")) {
    int idx1 = cmd.indexOf(':');
    int idx2 = cmd.indexOf(':', idx1 + 1);

    if (idx2 > 0) {
      String ssid = cmd.substring(idx1 + 1, idx2);
      String pass = cmd.substring(idx2 + 1);

      if (ssid.length() > 0 && ssid.length() < 32 && pass.length() < 64) {
        copyToBuffer(deviceConfig.wifi_ssid, sizeof(deviceConfig.wifi_ssid),
                     ssid);
        copyToBuffer(deviceConfig.wifi_password,
                     sizeof(deviceConfig.wifi_password), pass);
        deviceConfig.configured = true;
        Serial.println("OK: WiFi configured");
        Serial.println("Note: Use SAVE_CONFIG to persist");
      } else {
        Serial.println("ERROR: Invalid SSID length");
      }
    } else {
      Serial.println("ERROR: Format: SET_WIFI:ssid:password");
    }
  }

  // SET_MQTT:broker:port
  else if (cmdUpper.startsWith("SET_MQTT:")) {
    int idx1 = cmd.indexOf(':');
    int idx2 = cmd.indexOf(':', idx1 + 1);

    if (idx2 > 0) {
      String broker = cmd.substring(idx1 + 1, idx2);
      int port = cmd.substring(idx2 + 1).toInt();

      if (broker.length() > 0 && broker.length() < 128 && port > 0 &&
          port < 65536) {
        copyToBuffer(deviceConfig.mqtt_broker, sizeof(deviceConfig.mqtt_broker),
                     broker);
        deviceConfig.mqtt_port = port;
        Serial.println("OK: MQTT broker configured");
        Serial.println("Note: Use SAVE_CONFIG to persist");
      } else {
        Serial.println("ERROR: Invalid broker or port");
      }
    } else {
      Serial.println("ERROR: Format: SET_MQTT:broker:port");
    }
  }

  // SET_MQTT_AUTH:username:password
  else if (cmdUpper.startsWith("SET_MQTT_AUTH:")) {
    int idx1 = cmd.indexOf(':');
    int idx2 = cmd.indexOf(':', idx1 + 1);

    if (idx2 > 0) {
      String user = cmd.substring(idx1 + 1, idx2);
      String pass = cmd.substring(idx2 + 1);

      if (user.length() < 32 && pass.length() < 64) {
        copyToBuffer(deviceConfig.mqtt_username,
                     sizeof(deviceConfig.mqtt_username), user);
        copyToBuffer(deviceConfig.mqtt_password,
                     sizeof(deviceConfig.mqtt_password), pass);
        Serial.println("OK: MQTT auth configured");
      } else {
        Serial.println("ERROR: Invalid MQTT auth length");
      }
    } else {
      Serial.println("ERROR: Format: SET_MQTT_AUTH:username:password");
    }
  }

  // SET_API_SECRET:value
  else if (cmdUpper.startsWith("SET_API_SECRET:")) {
    String secret = cmd.substring(15);
    if (secret.length() < 64) {
      copyToBuffer(deviceConfig.api_secret, sizeof(deviceConfig.api_secret),
                   secret);
      Serial.println("OK: API secret updated");
    } else {
      Serial.println("ERROR: API secret too long (max 63 chars)");
    }
  }

  // SET_REQUIRE_SIGNED:1|0
  else if (cmdUpper.startsWith("SET_REQUIRE_SIGNED:")) {
    int val = cmd.substring(19).toInt();
    deviceConfig.requireSignedMessages = (val == 1);
    Serial.print("OK: Require signed messages ");
    Serial.println(deviceConfig.requireSignedMessages ? "enabled" : "disabled");
  }

  // SET_ALLOW_REMOTE_NETCFG:1|0
  else if (cmdUpper.startsWith("SET_ALLOW_REMOTE_NETCFG:")) {
    int val = cmd.substring(24).toInt();
    deviceConfig.allowRemoteNetworkConfig = (val == 1);
    Serial.print("OK: Remote network config ");
    Serial.println(deviceConfig.allowRemoteNetworkConfig ? "allowed"
                                                         : "disabled");
  }

  // SET_DEVICE_ID:name
  else if (cmdUpper.startsWith("SET_DEVICE_ID:")) {
    String devId = cmd.substring(14);
    if (devId.length() > 0 && devId.length() < 32) {
      copyToBuffer(deviceConfig.device_id, sizeof(deviceConfig.device_id),
                   devId);
      Serial.println("OK: Device ID set to " + devId);
    } else {
      Serial.println("ERROR: Invalid device ID");
    }
  }

  // SET_PRICE:amount
  else if (cmdUpper.startsWith("SET_PRICE:")) {
    int price = cmd.substring(10).toInt();
    if (price > 0 && price <= 100000) {
      deviceConfig.pricePerLiter = price;
      Serial.print("OK: Price set to ");
      Serial.print(price);
      Serial.println(" so'm per liter");
    } else {
      Serial.println("ERROR: Price must be 1-100000");
    }
  }

  // SET_TIMEOUT:seconds
  else if (cmdUpper.startsWith("SET_TIMEOUT:")) {
    int seconds = cmd.substring(12).toInt();
    if (seconds >= 60 && seconds <= 3600) {
      deviceConfig.sessionTimeout = seconds * 1000;
      Serial.print("OK: Timeout set to ");
      Serial.print(seconds);
      Serial.println(" seconds");
    } else {
      Serial.println("ERROR: Timeout must be 60-3600 seconds");
    }
  }

  // SET_FREE_WATER:1|0
  else if (cmdUpper.startsWith("SET_FREE_WATER:")) {
    int val = cmd.substring(15).toInt();
    deviceConfig.enableFreeWater = (val == 1);
    Serial.print("OK: Free water ");
    Serial.println(deviceConfig.enableFreeWater ? "enabled" : "disabled");
  }

  // SET_RELAY_ACTIVE:1|0
  else if (cmdUpper.startsWith("SET_RELAY_ACTIVE:")) {
    // Hardware policy: project relay is fixed Active-HIGH.
    deviceConfig.relayActiveHigh = true;
    config.relayActiveHigh = true;
    // Keep valve safely closed.
    setRelay(false);
    Serial.println("OK: Relay mode fixed to ACTIVE_HIGH");
  }

  // SET_FREE_WATER_COOLDOWN:seconds|ms
  else if (cmdUpper.startsWith("SET_FREE_WATER_COOLDOWN:")) {
    unsigned long raw = cmd.substring(24).toInt();
    unsigned long cooldown = normalizeSecondsOrMs(raw);
    if (cooldown >= 60000 && cooldown <= 7200000) {
      deviceConfig.freeWaterCooldown = cooldown;
      Serial.print("OK: Free water cooldown set to ");
      Serial.print(cooldown / 1000);
      Serial.println(" seconds");
    } else {
      Serial.println("ERROR: Cooldown must be 60-7200 seconds");
    }
  }

  // SET_FREE_WATER_AMOUNT:liters|ml
  else if (cmdUpper.startsWith("SET_FREE_WATER_AMOUNT:")) {
    float raw = cmd.substring(22).toFloat();
    float amount = normalizeFreeWaterAmount(raw);
    if (amount > 0.0f && amount <= 5.0f) {
      deviceConfig.freeWaterAmount = amount;
      Serial.print("OK: Free water amount set to ");
      Serial.print(amount * 1000.0f, 0);
      Serial.println(" ml");
    } else {
      Serial.println("ERROR: Amount must be 1-5000 ml");
    }
  }

  // SET_PULSES_PER_LITER:value
  else if (cmdUpper.startsWith("SET_PULSES_PER_LITER:")) {
    float pulses = cmd.substring(21).toFloat();
    if (pulses > 0.0f && pulses <= 5000.0f) {
      deviceConfig.pulsesPerLiter = pulses;
      Serial.print("OK: Pulses per liter set to ");
      Serial.println(pulses, 2);
    } else {
      Serial.println("ERROR: Pulses per liter must be 1-5000");
    }
  }

  // SET_TDS_THRESHOLD:value
  else if (cmdUpper.startsWith("SET_TDS_THRESHOLD:")) {
    int threshold = cmd.substring(18).toInt();
    if (threshold >= 0 && threshold <= 5000) {
      deviceConfig.tdsThreshold = threshold;
      Serial.print("OK: TDS threshold set to ");
      Serial.print(threshold);
      Serial.println(" ppm");
    } else {
      Serial.println("ERROR: TDS threshold must be 0-5000");
    }
  }

  // SET_TDS_TEMP:value
  else if (cmdUpper.startsWith("SET_TDS_TEMP:")) {
    float temp = cmd.substring(13).toFloat();
    if (temp >= 0.0f && temp <= 80.0f) {
      deviceConfig.tdsTemperatureC = temp;
      Serial.print("OK: TDS temperature set to ");
      Serial.print(temp, 1);
      Serial.println(" C");
    } else {
      Serial.println("ERROR: TDS temperature must be 0-80 C");
    }
  }

  // SET_TDS_CALIB:value
  else if (cmdUpper.startsWith("SET_TDS_CALIB:")) {
    float factor = cmd.substring(14).toFloat();
    if (factor > 0.0f && factor <= 5.0f) {
      deviceConfig.tdsCalibrationFactor = factor;
      Serial.print("OK: TDS calibration set to ");
      Serial.println(factor, 3);
    } else {
      Serial.println("ERROR: TDS calibration must be 0-5");
    }
  }

  // SET_CASH_PULSE:value
  else if (cmdUpper.startsWith("SET_CASH_PULSE:")) {
    int value = cmd.substring(15).toInt();
    if (value > 0 && value <= 100000) {
      deviceConfig.cashPulseValue = value;
      Serial.print("OK: Cash pulse value set to ");
      Serial.print(value);
      Serial.println(" so'm");
    } else {
      Serial.println("ERROR: Cash pulse value must be 1-100000");
    }
  }

  // SET_PAYMENT_INTERVAL:seconds|ms
  else if (cmdUpper.startsWith("SET_PAYMENT_INTERVAL:")) {
    unsigned long interval = cmd.substring(21).toInt();
    if (interval >= 200 && interval <= 600000) {
      deviceConfig.paymentCheckInterval = interval;
      Serial.print("OK: Payment interval set to ");
      Serial.print(interval);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: Payment interval must be 200-600000 ms");
    }
  }

  // SET_DISPLAY_INTERVAL:ms
  else if (cmdUpper.startsWith("SET_DISPLAY_INTERVAL:")) {
    unsigned long interval = cmd.substring(21).toInt();
    if (interval >= 50 && interval <= 10000) {
      deviceConfig.displayUpdateInterval = interval;
      Serial.print("OK: Display interval set to ");
      Serial.print(interval);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: Display interval must be 50-10000 ms");
    }
  }

  // SET_TDS_INTERVAL:seconds|ms
  else if (cmdUpper.startsWith("SET_TDS_INTERVAL:")) {
    unsigned long interval = cmd.substring(17).toInt();
    if (interval >= 1000 && interval <= 600000) {
      deviceConfig.tdsCheckInterval = interval;
      Serial.print("OK: TDS interval set to ");
      Serial.print(interval);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: TDS interval must be 1000-600000 ms");
    }
  }

  // SET_HEARTBEAT_INTERVAL:seconds|ms
  else if (cmdUpper.startsWith("SET_HEARTBEAT_INTERVAL:")) {
    unsigned long interval = cmd.substring(23).toInt();
    if (interval >= 1000 && interval <= 3600000) {
      deviceConfig.heartbeatInterval = interval;
      Serial.print("OK: Heartbeat interval set to ");
      Serial.print(interval);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: Heartbeat interval must be 1000-3600000 ms");
    }
  }

  // SET_PWR Removed

  // SET_CASH_GAP:ms

  // SET_CASH_GAP:ms
  else if (cmdUpper.startsWith("SET_CASH_GAP:")) {
    int gap = cmd.substring(13).toInt();
    if (gap >= 20 && gap <= 1000) {
      deviceConfig.cashPulseGapMs = gap;
      Serial.print("OK: Cash pulse gap set to ");
      Serial.print(gap);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: Cash pulse gap must be 20-1000 ms");
    }
  }

  // APPLY_CONFIG
  else if (cmdUpper == "APPLY_CONFIG") {
    applyRuntimeConfig();
    applyConfigStateEffects();
    setupWiFi();
    mqttClient.disconnect();
    mqttClient.setServer(deviceConfig.mqtt_broker, deviceConfig.mqtt_port);
    reconnectMQTT();
    Serial.println("OK: Configuration applied");
  }

  // SET_GROUP:groupId
  else if (cmdUpper.startsWith("SET_GROUP:")) {
    String groupId = cmd.substring(10);
    groupId.trim();
    if (groupId.length() > 0 && groupId.length() < 32) {
      strncpy(deviceConfig.groupId, groupId.c_str(), 31);
      deviceConfig.groupId[31] = '\0';
      saveConfigToStorage();
      generateMQTTTopics(); // Regenerate topics with new groupId
      Serial.print("OK: Group ID set to '");
      Serial.print(deviceConfig.groupId);
      Serial.println("'");
      Serial.println("Note: Reconnect MQTT to subscribe to group topics");
    } else {
      Serial.println("ERROR: Group ID must be 1-31 characters");
    }
  }

  // GET_GROUP
  else if (cmdUpper == "GET_GROUP") {
    if (strlen(deviceConfig.groupId) > 0) {
      Serial.print("Group ID: ");
      Serial.println(deviceConfig.groupId);
    } else {
      Serial.println("Group ID: (not set)");
    }
  }

  // SAVE_CONFIG
  else if (cmdUpper == "SAVE_CONFIG") {
    saveConfigToStorage();
    Serial.println("OK: Configuration saved to EEPROM");
  }

  // LOAD_CONFIG
  else if (cmdUpper == "LOAD_CONFIG") {
    loadConfigFromStorage();
    Serial.println("OK: Configuration reloaded from EEPROM");
    printCurrentConfig();
  }

  // FACTORY_RESET
  else if (cmdUpper == "FACTORY_RESET") {
    Serial.println("WARNING: This will reset all settings!");
    Serial.println("Type 'YES' to confirm...");

    unsigned long startTime = millis();
    while (millis() - startTime < 10000) {
      if (Serial.available()) {
        String confirm = Serial.readStringUntil('\n');
        confirm.trim();
        String confirmUpper = confirm;
        confirmUpper.toUpperCase();

        if (confirmUpper == "YES") {
          loadDefaultConfig();
          saveConfigToStorage();
          Serial.println("OK: Factory reset completed");
          Serial.println("Device will restart in 3 seconds...");
          delay(3000);
          ESP.restart();
          return;
        } else {
          Serial.println("CANCELLED: Factory reset aborted");
          return;
        }
      }
      delay(10);
    }
    Serial.println("TIMEOUT: Factory reset aborted");
  }

  // GET_STATUS
  else if (cmdUpper == "GET_STATUS") {
    showStatus();
  }

  // RESTART
  else if (cmdUpper == "RESTART") {
    Serial.println("OK: Restarting device...");
    delay(500);
    ESP.restart();
  }

  // HELP
  else if (cmdUpper == "HELP") {
    showHelp();
  }

  // TEST functionality for hardware debugging
  else if (cmdUpper.startsWith("TEST ")) {
    String subCmd = cmdUpper.substring(5);

    if (subCmd.startsWith("RELAY ")) {
      String action = subCmd.substring(6);
      if (action == "ON") {
        Serial.println(
            "TEST: Forcing Relay ON (Logic Level depends on config)");
        setRelay(true);
      } else if (action == "OFF") {
        Serial.println("TEST: Forcing Relay OFF");
        setRelay(false);
      } else if (action.startsWith("RAW ")) {
        int level = action.substring(4).toInt();
        Serial.print("TEST: Forcing Relay Pin RAW ");
        Serial.println(level ? "HIGH" : "LOW");
        digitalWrite(RELAY_PIN, level ? HIGH : LOW);
      } else {
        Serial.println("ERROR: TEST RELAY [ON|OFF|RAW 0|RAW 1]");
      }
    } else {
      Serial.println("ERROR: Unknown test command");
    }
  }

  // Unknown command
  else {
    Serial.println(
        "ERROR: Unknown command. Type 'HELP' for available commands");
  }

  Serial.println(); // Blank line for readability
}

// ============================================
// SHOW HELP
// ============================================
void showHelp() {
  Serial.println("\n========== AVAILABLE COMMANDS ==========");
  Serial.println("\n[Configuration]");
  Serial.println(
      "  GET_CONFIG                       - Show current configuration");
  Serial.println("  SET_WIFI:ssid:password           - Set WiFi credentials");
  Serial.println("  SET_MQTT:broker:port             - Set MQTT broker");
  Serial.println(
      "  SET_MQTT_AUTH:user:pass          - Set MQTT authentication");
  Serial.println("  SET_DEVICE_ID:name               - Set device identifier");
  Serial.println(
      "  SET_PRICE:amount                 - Set price per liter (so'm)");
  Serial.println("  SET_TIMEOUT:seconds              - Set session timeout");
  Serial.println(
      "  SET_FREE_WATER:1|0               - Enable/disable free water");
  Serial.println(
      "  SET_RELAY_ACTIVE:1|0             - Relay mode (forced ACTIVE_HIGH)");
  Serial.println("  SET_API_SECRET:value             - Set API signing secret");
  Serial.println(
      "  SET_REQUIRE_SIGNED:1|0           - Require signed MQTT messages");
  Serial.println(
      "  SET_ALLOW_REMOTE_NETCFG:1|0      - Allow WiFi/MQTT via MQTT");
  Serial.println(
      "  SET_CASH_PULSE:value             - Cash acceptor so'm per pulse");
  Serial.println("  SET_CASH_GAP:ms                  - Cash pulse gap (ms)");
  Serial.println("  SET_FREE_WATER_COOLDOWN:sec      - Free water cooldown");
  Serial.println("  SET_FREE_WATER_AMOUNT:ml         - Free water amount");
  Serial.println(
      "  SET_PULSES_PER_LITER:value       - Flow sensor calibration");
  Serial.println("  SET_TDS_THRESHOLD:ppm            - TDS warning threshold");
  Serial.println("  SET_TDS_TEMP:celsius             - TDS temperature");
  Serial.println("  SET_TDS_CALIB:factor             - TDS calibration factor");
  Serial.println("  SET_PAYMENT_INTERVAL:ms          - Payment check interval");
  Serial.println(
      "  SET_DISPLAY_INTERVAL:ms          - Display refresh interval");
  Serial.println("  SET_TDS_INTERVAL:ms              - TDS check interval");
  Serial.println("  SET_HEARTBEAT_INTERVAL:ms        - Heartbeat interval");
  Serial.println("  APPLY_CONFIG                     - Apply settings now");

  Serial.println("\n[Storage]");
  Serial.println(
      "  SAVE_CONFIG                      - Save configuration to EEPROM");
  Serial.println("  LOAD_CONFIG                      - Reload from EEPROM");
  Serial.println(
      "  FACTORY_RESET                    - Reset to factory defaults");

  Serial.println("\n[System]");
  Serial.println("  SAVE_CONFIG              - Save config to flash");
  Serial.println("  SET_GROUP:id             - Set group ID for fleet");
  Serial.println("  GET_GROUP                - Show current group ID");
  Serial.println("  GET_STATUS                       - Show device status");
  Serial.println("  RESTART                          - Restart device");
  Serial.println("  TEST RELAY [ON|OFF|RAW 0|1]      - Test relay hardware");
  Serial.println("  HELP                             - Show this help message");

  Serial.println("\n========================================");
}

// ============================================
// SHOW STATUS
// ============================================
void showStatus() {
  Serial.println("\n========== DEVICE STATUS ==========");

  // WiFi Status
  Serial.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("Disconnected");
  }

  // System
  Serial.print("\nSystem State: ");
  switch (currentState) {
  case IDLE:
    Serial.println("IDLE");
    break;
  case ACTIVE:
    Serial.println("ACTIVE");
    break;
  case DISPENSING:
    Serial.println("DISPENSING");
    break;
  case PAUSED:
    Serial.println("PAUSED");
    break;
  case FREE_WATER:
    Serial.println("FREE_WATER");
    break;
  }

  Serial.print("Balance: ");
  Serial.print(balance);
  Serial.println(" so'm");

  Serial.println("Relay Mode: ACTIVE_HIGH (forced)");
  Serial.print("Relay Pin Level: ");
  Serial.println(digitalRead(RELAY_PIN) == HIGH ? "HIGH (ON)" : "LOW (OFF)");

  Serial.print("Dispensed: ");
  Serial.print(totalDispensedLiters, 2);
  Serial.println(" L");

  Serial.print("TDS: ");
  Serial.print(tdsPPM);
  Serial.println(" ppm");

  Serial.print("\nUptime: ");
  unsigned long uptime = millis() / 1000;
  Serial.print(uptime / 3600);
  Serial.print("h ");
  Serial.print((uptime % 3600) / 60);
  Serial.print("m ");
  Serial.print(uptime % 60);
  Serial.println("s");

  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.println("==================================\n");
}
