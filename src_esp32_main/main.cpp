
/*
 * ========================================
 * ESP32 #2 - MAIN CONTROLLER
 * ========================================
 * eWater Vending Machine
 *
 * Bu ESP32 asosiy controller:
 *   - Relay (Solenoid Valve)
 *   - START/PAUSE Buttons
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
#include "local_events.h"
#include "relay_control.h"
#include "state_machine.h"
#include "uart_receiver.h" // Replaces payment.h - receives from Payment ESP32
#include <cstdio>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h> // Hardware Watchdog Timer

// ============================================
// TIMERS (Non-blocking)
// ============================================
// Note: lastPaymentCheck removed - payments now come via UART
unsigned long lastHeartbeat = 0;
// Globals from state_machine.cpp
extern unsigned long lastSessionActivity;

// Constants
const int WATCHDOG_TIMEOUT_SECONDS = 30;
const uint8_t SAFE_MODE_FAULT_THRESHOLD = 3;
static bool safeModeActive = false;
static uint8_t bootFaultCount = 0;
static esp_reset_reason_t lastResetReason = ESP_RST_UNKNOWN;

static bool isFaultResetReason(esp_reset_reason_t reason) {
  return (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT ||
          reason == ESP_RST_WDT || reason == ESP_RST_PANIC ||
          reason == ESP_RST_BROWNOUT);
}

static uint8_t updateBootFaultCounter(esp_reset_reason_t reason) {
  Preferences bootPrefs;
  if (!bootPrefs.begin("ewater", false)) {
    return 0;
  }

  uint8_t faults = bootPrefs.getUChar("boot_faults", 0);
  if (isFaultResetReason(reason)) {
    if (faults < 250) {
      faults++;
    }
  } else {
    faults = 0;
  }
  bootPrefs.putUChar("boot_faults", faults);
  bootPrefs.end();
  return faults;
}

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

  lastResetReason = esp_reset_reason();
  DEBUG_PRINTLN("\n\n=== VENDING MACHINE STARTING ===");
  DEBUG_PRINT("Reset reason: ");
  DEBUG_PRINTLN(resetReasonToString(lastResetReason));

  bootFaultCount = updateBootFaultCounter(lastResetReason);
  if (bootFaultCount >= SAFE_MODE_FAULT_THRESHOLD) {
    safeModeActive = true;
    DEBUG_PRINTLN("⚠️ SAFE MODE activated due to repeated fault resets");
  }

  // ============================================
  // HARDWARE WATCHDOG TIMER
  // ============================================
  DEBUG_PRINTLN("Enabling Hardware Watchdog...");
  esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true); // Auto-reboot enabled
  esp_task_wdt_add(NULL); // Add current task to watchdog
  DEBUG_PRINTLN("✓ Watchdog enabled - system will auto-recover from freezes");

  // GPIO Setup
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);

  // Load config from EEPROM FIRST
  initConfigStorage();
  initConfig();
  initDisplay();
  setDispenseOutputsActive(false);

  // Ensure relay is OFF (ACTIVE_HIGH hardware policy)
  setDispenseOutputsActive(false);
  DEBUG_PRINT("Relay boot check (OFF) pin level: ");
  DEBUG_PRINTLN(digitalRead(RELAY_PIN) == HIGH ? "HIGH" : "LOW");
  initStateMachine();

  if (safeModeActive) {
    currentState = IDLE;
    balance = 0;
    DEBUG_PRINTLN("SAFE MODE: outputs locked OFF");
  } else {
    initUartReceiver(); // UART from Payment ESP32 (replaces initPayment)
  }

  DEBUG_PRINTLN("=== SYSTEM READY ===\n");
  DEBUG_PRINT("Firmware Version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  char msg[64];
  snprintf(msg, sizeof(msg), "Device started %s", FIRMWARE_VERSION);
  publishLog("SYSTEM", msg);
  showTemporaryMessage("READY", safeModeActive ? "SAFE MODE" : "LOCAL MODE");
}

// ============================================
// MAIN LOOP - Fully Async
// ============================================
void loop() {
  // Reset watchdog timer - "I'm alive!"
  esp_task_wdt_reset();

  unsigned long now = millis();

  if (safeModeActive) {
    // Keep actuator safe until a clean reboot.
    if (isRelayOn() || isRelay2On()) {
      setDispenseOutputsActive(false);
    }
    if (currentState != IDLE) {
      currentState = IDLE;
      balance = 0;
    }

    processConfigSave();
    updateDisplay();
    delay(5);
    return;
  }

  // Task 1: UART Payment Check (from ESP32 #1)
  // Payments now come via UART from Payment ESP32
  processUartReceiver();

  // Task 2: Session Timeout
  if (currentState != IDLE) {
    if (millis() - lastSessionActivity >= config.sessionTimeout) {
      handleSessionTimeout();
    }
  }

  // Task 3: Heartbeat
  if (now - lastHeartbeat >= config.heartbeatInterval) {
    lastHeartbeat = now;
    publishStatus();
    DEBUG_PRINT("LOCAL_HEARTBEAT uptime=");
    DEBUG_PRINT(millis() / 1000);
    DEBUG_PRINT(" heap=");
    DEBUG_PRINT(ESP.getFreeHeap());
    DEBUG_PRINT(" reset=");
    DEBUG_PRINT(resetReasonToString(lastResetReason));
    DEBUG_PRINT(" safe_mode=");
    DEBUG_PRINT(safeModeActive ? "YES" : "NO");
    DEBUG_PRINT(" boot_faults=");
    DEBUG_PRINT(bootFaultCount);
    DEBUG_PRINT(" uart_parse_errors=");
    DEBUG_PRINT(getUartParseErrorCount());
    DEBUG_PRINT(" uart_frame_drops=");
    DEBUG_PRINTLN(getUartFrameDropCount());
  }

  if (currentState == DISPENSING) {
    serviceRelayAutomation();
  }

  // Task 4: Button Handling (edge-based debounce)
  const unsigned long DEBOUNCE_MS = 100;
  const unsigned long BUTTON_ACTION_GAP_MS = 650;
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

  // Task 5: Deferred config save (debounced)
  processConfigSave();
  updateDisplay();

  // Tasks Removed: PowerManager, Analytics, SystemHealth

  delay(1); // Yield to keep watchdog happy without blocking too long
}
