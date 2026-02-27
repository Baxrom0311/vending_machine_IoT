
/*
 * ========================================
 * ESP32 #2 - MAIN CONTROLLER
 * ========================================
 * eWater Vending Machine
 *
 * Bu ESP32 asosiy controller:
 *   - Display (LCD 20x4)
 *   - Relay (Solenoid Valve)
 *   - Flow Sensor, TDS Sensor
 *   - START/PAUSE Buttons
 *   - WiFi / MQTT
 *   - UART orqali Payment ESP32 dan xabar oladi
 *
 * Payment ESP32 dan UART orqali pul qabul qiladi.
 * Cash acceptor bu ESP32 da yo'q!
 *
 * Author: VendingBot Team
 * Version: 2.4.0 - Dual ESP32 Architecture
 */

#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "diagnostics.h"
#include "display.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include "sensors.h"
#include "serial_config.h"
#include "state_machine.h"
#include "uart_receiver.h" // Replaces payment.h - receives from Payment ESP32
#include <ArduinoJson.h>   // Required for heartbeat
#include <WiFi.h>          // For heartbeat WiFi.localIP() and WiFi.RSSI()
#include <cstdio>
#include <esp_system.h>
#include <esp_task_wdt.h> // Hardware Watchdog Timer

// ============================================
// TIMERS (Non-blocking)
// ============================================
// Note: lastPaymentCheck removed - payments now come via UART
unsigned long lastDisplayUpdate = 0;
unsigned long lastTdsCheck = 0;
unsigned long lastHeartbeat = 0;
// Globals from state_machine.cpp
extern unsigned long lastSessionActivity;
extern unsigned long freeWaterAvailableTime;
extern bool freeWaterUsed;

// Constants
const int WATCHDOG_TIMEOUT_SECONDS = 30;
const unsigned long MQTT_STALL_RECOVERY_MS = 15UL * 60UL * 1000UL;
const unsigned long NETWORK_RECOVERY_COOLDOWN_MS = 10UL * 60UL * 1000UL;
static unsigned long lastMqttHealthyMs = 0;
static unsigned long lastNetworkRecoveryMs = 0;

static const char *resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_SW:
    return "SW";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WDT";
  case ESP_RST_TASK_WDT:
    return "TASK_WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_SDIO:
    return "SDIO";
  default:
    return "UNKNOWN";
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(100); // Wait for serial

  DEBUG_PRINTLN("\n\n=== VENDING MACHINE STARTING ===");
  DEBUG_PRINT("Reset reason: ");
  DEBUG_PRINTLN(resetReasonToString(esp_reset_reason()));

  // ============================================
  // HARDWARE WATCHDOG TIMER
  // ============================================
  DEBUG_PRINTLN("Enabling Hardware Watchdog...");
  esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true); // Auto-reboot enabled
  esp_task_wdt_add(NULL); // Add current task to watchdog
  DEBUG_PRINTLN("✓ Watchdog enabled - system will auto-recover from freezes");

  // GPIO Setup
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);

  setRelay(false);

  // Initialize modules
  initDisplay();

  // Load config from EEPROM FIRST
  initConfigStorage();

  // Initialize serial config interface
  initSerialConfig();

  const bool configured = isConfigured();
  if (!configured) {
    DEBUG_PRINTLN("\n⚠️  DEVICE NOT CONFIGURED!");
    DEBUG_PRINTLN("Offline mode will run (cash only).");
    DEBUG_PRINTLN("Configure via Serial interface (type HELP)\n");
  }

  // Now initialize with loaded config
  initConfig();
  // Ensure relay is OFF (ACTIVE_HIGH hardware policy)
  setRelay(false);
  DEBUG_PRINT("Relay boot check (OFF) pin level: ");
  DEBUG_PRINTLN(digitalRead(RELAY_PIN) == HIGH ? "HIGH" : "LOW");
  initSensors();
  initStateMachine();
  initUartReceiver(); // UART from Payment ESP32 (replaces initPayment)

  // WiFi / MQTT (only if configured)
  if (configured) {
    setupWiFi();
    setupMQTT();
  }

  lastMqttHealthyMs = millis();
  lastNetworkRecoveryMs = 0;

  DEBUG_PRINTLN("=== SYSTEM READY ===\n");
  DEBUG_PRINT("Firmware Version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  if (configured) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Device started %s", FIRMWARE_VERSION);
    publishLog("SYSTEM", msg);
  }
}

// ============================================
// MAIN LOOP - Fully Async
// ============================================
void loop() {
  // Reset watchdog timer - "I'm alive!"
  esp_task_wdt_reset();

  unsigned long now = millis();

  // WiFi connection state machine
  if (isConfigured()) {
    processWiFi();

    // MQTT Connection - only if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqttClient.connected()) {
        // CRITICAL FIX: Only attempt reconnection if IDLE to prevent blocking
        // during dispensing
        if (currentState == IDLE) {
          reconnectMQTT();
        }
      } else {
        // Only process MQTT loop if connected
        mqttClient.loop();
      }
    }

    const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected && mqttClient.connected()) {
      lastMqttHealthyMs = now;
    } else if (!wifiConnected) {
      // Network is genuinely down, keep timer fresh to avoid false "stalled"
      // recovery triggers.
      lastMqttHealthyMs = now;
    } else if (currentState == IDLE &&
               (now - lastMqttHealthyMs) >= MQTT_STALL_RECOVERY_MS &&
               (now - lastNetworkRecoveryMs) >=
                   NETWORK_RECOVERY_COOLDOWN_MS) {
      lastNetworkRecoveryMs = now;
      DEBUG_PRINTLN("⚠️ MQTT stalled too long, restarting WiFi stack");
      mqttClient.disconnect();
      setupWiFi();
      reconnectMQTT();
      lastMqttHealthyMs = now;
    }
  }

  // Process network config apply/rollback
  if (isConfigured()) {
    processNetworkApply();
  }

  // Task 1: UART Payment Check (from ESP32 #1)
  // Payments now come via UART from Payment ESP32
  processUartReceiver();

  // Task 2: Display Update
  if (now - lastDisplayUpdate >= config.displayUpdateInterval) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // Task 3: TDS Check
  if (now - lastTdsCheck >= config.tdsCheckInterval) {
    lastTdsCheck = now;
    tdsPPM = readTDS();
    publishTDS();
  }

  // Task 4: Session Timeout
  // Uses millis() directly (not stale `now`) to prevent unsigned underflow.
  // Applies to all non-IDLE states:
  //   ACTIVE/PAUSED: timeout if user doesn't press START
  //   DISPENSING/FREE_WATER: timeout if flow sensor stops (safety)
  if (currentState != IDLE) {
    if (millis() - lastSessionActivity >= config.sessionTimeout) {
      handleSessionTimeout();
    }
  }

  // Task 5: Free Water Timer
  if (config.enableFreeWater && currentState == IDLE) {
    if (freeWaterUsed && now >= freeWaterAvailableTime) {
      freeWaterUsed = false;
    }
    if (!freeWaterUsed && now >= freeWaterAvailableTime) {
      // Display will show free water offer
      // Logic handled in updateDisplay usually
    }
  }

  // Task 6: Heartbeat
  if (now - lastHeartbeat >= config.heartbeatInterval) {
    lastHeartbeat = now;
    publishStatus();

    // MEDIUM FIX: Heartbeat with all required fields per MQTT_API.md
    JsonDocument hb;
    hb["status"] = "online"; // MEDIUM FIX: Added status field
    hb["uptime"] = millis() / 1000;
    hb["ip"] = WiFi.localIP().toString();
    hb["rssi"] = WiFi.RSSI();
    hb["ssid"] = WiFi.SSID(); // LOW FIX: Added ssid for UI
    hb["firmware_version"] = FIRMWARE_VERSION;
    hb["free_heap"] = ESP.getFreeHeap();
    String hbStr;
    serializeJson(hb, hbStr);
    publishMQTT(TOPIC_HEARTBEAT, hbStr.c_str());
  }

  // Task 7: Button Handling (edge-based debounce)
  const unsigned long DEBOUNCE_MS = 100;
  const unsigned long BUTTON_ACTION_GAP_MS = 450;
  static int startLastRaw = HIGH;
  static int startStable = HIGH;
  static unsigned long startLastChangeMs = 0;
  static int pauseLastRaw = HIGH;
  static int pauseStable = HIGH;
  static unsigned long pauseLastChangeMs = 0;
  static unsigned long lastButtonActionMs = 0;
  const unsigned long buttonNow = millis();

  const int startRaw = digitalRead(START_BUTTON_PIN);
  if (startRaw != startLastRaw) {
    startLastRaw = startRaw;
    startLastChangeMs = buttonNow;
  }
  if ((buttonNow - startLastChangeMs) >= DEBOUNCE_MS &&
      startStable != startLastRaw) {
    startStable = startLastRaw;
    if (startStable == LOW &&
        (buttonNow - lastButtonActionMs) >= BUTTON_ACTION_GAP_MS) {
      DEBUG_PRINT("▶️ START pressed! State=");
      DEBUG_PRINTLN(currentState);
      lastButtonActionMs = buttonNow;
      handleStartButton();
    }
  }

  const int pauseRaw = digitalRead(PAUSE_BUTTON_PIN);
  if (pauseRaw != pauseLastRaw) {
    pauseLastRaw = pauseRaw;
    pauseLastChangeMs = buttonNow;
  }
  if ((buttonNow - pauseLastChangeMs) >= DEBOUNCE_MS &&
      pauseStable != pauseLastRaw) {
    pauseStable = pauseLastRaw;
    if (pauseStable == LOW &&
        (buttonNow - lastButtonActionMs) >= BUTTON_ACTION_GAP_MS) {
      DEBUG_PRINT("⏸️ PAUSE pressed! State=");
      DEBUG_PRINTLN(currentState);
      lastButtonActionMs = buttonNow;
      handlePauseButton();
    }
  }

  // Task 8: Flow Sensor Processing
  if (currentState == DISPENSING || currentState == FREE_WATER) {
    processFlowSensor();
    // HIGH FIX: lastSessionActivity is now updated ONLY when actual flow
    // detected (inside processFlowSensor when litersDiff >= 0.01) This ensures
    // valve closes if flow sensor fails (no fake activity)
  }

  // Task 9: Serial Configuration
  handleSerialConfig();

  // Deferred config save (debounced)
  processConfigSave();

  // Tasks Removed: PowerManager, Analytics, SystemHealth

  delay(1); // Yield to keep watchdog happy without blocking too long
}
