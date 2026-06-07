#include "serial_config.h"
#include "config.h"
#include "config_storage.h"
#include "hardware.h"
#include "relay_control.h"
#include "sensors.h"
#include "state_machine.h"
#include "uart_receiver.h"
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

static bool awaitingFactoryResetConfirm = false;
static unsigned long factoryResetPromptMs = 0;
static constexpr unsigned long FACTORY_RESET_CONFIRM_TIMEOUT_MS = 10000UL;

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

  if (awaitingFactoryResetConfirm &&
      (millis() - factoryResetPromptMs) > FACTORY_RESET_CONFIRM_TIMEOUT_MS) {
    awaitingFactoryResetConfirm = false;
    Serial.println("TIMEOUT: Factory reset aborted");
    Serial.println();
  }

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

  if (awaitingFactoryResetConfirm) {
    awaitingFactoryResetConfirm = false;
    if (cmdUpper == "YES") {
      flushUartReceiverPersistence();
      loadDefaultConfig();
      saveConfigToStorage();
      Serial.println("OK: Factory reset completed");
      Serial.println("Device will restart in 3 seconds...");
      delay(3000);
      ESP.restart();
      return;
    }
    Serial.println("CANCELLED: Factory reset aborted");
    Serial.println();
    return;
  }

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

  // SET_DEVICE_ID:name
  else if (cmdUpper.startsWith("SET_DEVICE_ID:")) {
    String devId = cmd.substring(14);
    if (devId.length() > 0 && devId.length() < 32) {
      copyToBuffer(deviceConfig.device_id, sizeof(deviceConfig.device_id),
                   devId);
      deviceConfig.configured = true;
      Serial.println("OK: Device ID set to " + devId);
    } else {
      Serial.println("ERROR: Invalid device ID");
    }
  }

  // SET_PRICE:amount
  else if (cmdUpper.startsWith("SET_PRICE:")) {
    int price = cmd.substring(10).toInt();
    if (price >= 100 && price <= 100000) {
      deviceConfig.pricePerLiter = price;
      Serial.print("OK: Price set to ");
      Serial.print(price);
      Serial.println(" so'm per liter");
    } else {
      Serial.println("ERROR: Price must be 100-100000");
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

  // SET_RELAY_ACTIVE:1|0
  else if (cmdUpper.startsWith("SET_RELAY_ACTIVE:")) {
    String raw = cmd.substring(17);
    raw.trim();
    if (raw == "1" || raw == "0") {
      deviceConfig.relayActiveHigh = (raw == "1");
      config.relayActiveHigh = deviceConfig.relayActiveHigh;
      // Apply polarity immediately while keeping valve safely closed.
      setRelay(false);
      Serial.print("OK: Relay mode set to ");
      Serial.println(deviceConfig.relayActiveHigh ? "ACTIVE_HIGH"
                                                  : "ACTIVE_LOW");
    } else {
      Serial.println("ERROR: Use SET_RELAY_ACTIVE:1 or SET_RELAY_ACTIVE:0");
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
    if (interval >= 100 && interval <= 10000) {
      deviceConfig.displayUpdateInterval = interval;
      Serial.print("OK: Display interval set to ");
      Serial.print(interval);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: Display interval must be 100-10000 ms");
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
    if (interval >= 5000 && interval <= 3600000) {
      deviceConfig.heartbeatInterval = interval;
      Serial.print("OK: Heartbeat interval set to ");
      Serial.print(interval);
      Serial.println(" ms");
    } else {
      Serial.println("ERROR: Heartbeat interval must be 5000-3600000 ms");
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
    if (deviceConfig.wifi_ssid[0] != '\0') {
      setupWiFi();
    }
    Serial.println("OK: Configuration applied");
  }

  // SAVE_CONFIG
  else if (cmdUpper == "SAVE_CONFIG") {
    deviceConfig.configured = true;
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
    awaitingFactoryResetConfirm = true;
    factoryResetPromptMs = millis();
  }

  // GET_STATUS
  else if (cmdUpper == "GET_STATUS") {
    showStatus();
  }

  // RESTART
  else if (cmdUpper == "RESTART") {
    Serial.println("OK: Restarting device...");
    flushUartReceiverPersistence();
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
  Serial.println("  SET_WIFI:ssid:password           - Set optional WiFi credentials");
  Serial.println("  SET_DEVICE_ID:name               - Set device identifier");
  Serial.println(
      "  SET_PRICE:amount                 - Set price per liter (so'm)");
  Serial.println("  SET_TIMEOUT:seconds              - Set session timeout");
  Serial.println(
      "  SET_RELAY_ACTIVE:1|0             - Relay mode (1=ACTIVE_HIGH, 0=ACTIVE_LOW)");
  Serial.println(
      "  SET_CASH_PULSE:value             - Cash acceptor so'm per pulse");
  Serial.println("  SET_CASH_GAP:ms                  - Cash pulse gap (ms)");
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
  }

  Serial.print("Balance: ");
  Serial.print(balance);
  Serial.println(" so'm");

  Serial.print("Relay Mode: ");
  Serial.println(config.relayActiveHigh ? "ACTIVE_HIGH" : "ACTIVE_LOW");
  Serial.print("Relay Pin Level: ");
  Serial.println(digitalRead(RELAY_PIN) == HIGH ? "HIGH" : "LOW");
  Serial.print("Relay Logical State: ");
  Serial.println(isRelayOn() ? "ON" : "OFF");

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
  Serial.print("Min Free Heap: ");
  Serial.print(ESP.getMinFreeHeap());
  Serial.println(" bytes");

  Serial.println("==================================\n");
}
