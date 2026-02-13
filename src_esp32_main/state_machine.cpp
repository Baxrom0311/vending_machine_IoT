#include "state_machine.h"
#include "config.h"
#include "display.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "relay_control.h"

// Note: lastSessionActivity is declared extern in state_machine.h

// ============================================
// GLOBAL STATE VARIABLES
// ============================================
SystemState currentState = IDLE;
volatile long balance = 0;
float totalDispensedLiters = 0.0;
float sessionStartBalance = 0.0;

volatile unsigned long flowPulseCount = 0;
float lastDispensedLiters = 0.0;

float freeWaterDispensed = 0.0;
bool freeWaterUsed = false;

unsigned long lastSessionActivity = 0;
unsigned long freeWaterAvailableTime = 0;
static SystemState pausedFromState = IDLE;

// ============================================
// INITIALIZATION
// ============================================
void initStateMachine() {
  currentState = IDLE;
  balance = 0;
  totalDispensedLiters = 0.0;
  sessionStartBalance = 0.0;
  flowPulseCount = 0;
  lastDispensedLiters = 0.0;
  freeWaterDispensed = 0.0;
  freeWaterUsed = false;
  lastSessionActivity = millis();
  freeWaterAvailableTime = millis() + config.freeWaterCooldown;
  pausedFromState = IDLE;
}

// ============================================
// SESSION TIMER
// ============================================
void resetSessionTimer() { lastSessionActivity = millis(); }

// ============================================
// APPLY CONFIG EFFECTS (Runtime)
// ============================================
void applyConfigStateEffects() {
  if (!config.enableFreeWater) {
    if (currentState == FREE_WATER) {
      currentState = IDLE;
      setRelay(false);
      publishLog("FREE_WATER", "Disabled");
      publishStatus();
    }
    freeWaterUsed = true;
    return;
  }

  if (currentState == IDLE) {
    freeWaterUsed = false;
    freeWaterAvailableTime = millis() + config.freeWaterCooldown;
  }
}

// ============================================
// SESSION TIMEOUT HANDLER
// ============================================
void handleSessionTimeout() {
  Serial.println("Session timeout!");

  // Log lost balance
  if (balance > 0) {
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg),
             "{\"event\":\"TIMEOUT\",\"balance_lost\":%.2f,\"dispensed\":%.2f}",
             (float)balance, totalDispensedLiters);
    publishLog("TIMEOUT", logMsg);
  }

  // Reset
  balance = 0;
  totalDispensedLiters = 0.0;
  sessionStartBalance = 0.0;
  currentState = IDLE;
  pausedFromState = IDLE;

  setRelay(false); // Close valve

  // Start free water timer
  freeWaterAvailableTime = millis() + config.freeWaterCooldown;
  freeWaterUsed = false;

  publishStatus();
}

// ============================================
// START BUTTON HANDLER
// ============================================
void handleStartButton() {
  resetSessionTimer();

  switch (currentState) {
  case IDLE:
    if (balance > 0) {
      currentState = DISPENSING;
      flowPulseCount = 0;
      lastDispensedLiters = 0.0;
      sessionStartBalance = balance;
      setRelay(true);

      // Dispense started
      publishLog("DISPENSE", "Started");
      publishStatus();
    } else if (config.enableFreeWater && millis() >= freeWaterAvailableTime &&
               !freeWaterUsed) {
      currentState = FREE_WATER;
      freeWaterDispensed = 0.0;
      flowPulseCount = 0;
      lastDispensedLiters = 0.0;
      setRelay(true);

      // Free water started
      publishLog("FREE_WATER", "Started");
      publishStatus();
    } else {
      // Feedback for user why start didn't work
      showTemporaryMessage("PUL KIRITING", "Yoki kuting...");
    }
    break;

  case ACTIVE:
    if (balance > 0) {
      currentState = DISPENSING;
      flowPulseCount = 0;
      lastDispensedLiters = 0.0;
      sessionStartBalance = balance;
      setRelay(true);

      // Dispense started
      publishLog("DISPENSE", "Started");
      publishStatus();
    }
    break;

  case PAUSED:
    // Resume the correct mode (paid/free) based on what was paused.
    if (pausedFromState == FREE_WATER) {
      if (config.enableFreeWater && !freeWaterUsed &&
          freeWaterDispensed < config.freeWaterAmount) {
        currentState = FREE_WATER;
        flowPulseCount = 0;
        lastDispensedLiters = 0.0;
        setRelay(true);

        pausedFromState = IDLE;
        publishLog("FREE_WATER", "Resumed");
        publishStatus();
        break;
      }
      // Free water no longer valid, fall back to paid dispensing if possible.
    }

    if (balance > 0) {
      currentState = DISPENSING;
      flowPulseCount = 0;
      lastDispensedLiters = 0.0;
      setRelay(true);

      pausedFromState = IDLE;
      publishLog("DISPENSE", "Resumed");
      publishStatus();
    } else {
      showTemporaryMessage("PUL KIRITING", "Yoki kuting...");
    }
    break;

  default:
    break;
  }
}

// ============================================
// PAUSE BUTTON HANDLER
// ============================================
void handlePauseButton() {
  resetSessionTimer();

  if (currentState == DISPENSING || currentState == FREE_WATER) {
    const SystemState prevState = currentState;
    pausedFromState = prevState;
    currentState = PAUSED;
    setRelay(false);

    Serial.println("PAUSE button pressed - Relay OFF");
    char msg[32];
    if (prevState == DISPENSING) {
      snprintf(msg, sizeof(msg), "%.2f", totalDispensedLiters);
      publishLog("PAUSE", msg);
    } else {
      snprintf(msg, sizeof(msg), "%.2f", freeWaterDispensed);
      publishLog("PAUSE_FREE", msg);
    }
    publishStatus();
  }
}

// ============================================
// FLOW SENSOR PROCESSING
// ============================================
void processFlowSensor() {
  if (config.pulsesPerLiter <= 0.0f) {
    return;
  }

  // Atomic read with overflow protection
  unsigned long pulses = 0;
  noInterrupts();
  // Overflow protection - reset at 1M pulses (~450L @ 2200 pulses/L)
  const unsigned long FLOW_COUNTER_MAX = 1000000UL;
  if (flowPulseCount > FLOW_COUNTER_MAX) {
    Serial.println("⚠️ Flow counter reset (normal overflow prevention)");
    flowPulseCount = 0;
    lastDispensedLiters = 0.0;
  }
  pulses = flowPulseCount;
  interrupts();

  float currentLiters = pulses / config.pulsesPerLiter;
  float litersDiff = currentLiters - lastDispensedLiters;

  if (litersDiff >= 0.01) { // Every 10ml
    lastDispensedLiters = currentLiters;

    // HIGH FIX: Update lastSessionActivity ONLY when actual flow detected
    // This prevents timeout during active dispensing but allows timeout if flow
    // stops
    lastSessionActivity = millis();

    if (currentState == DISPENSING) {
      // Deduct balance - FIX: Check BEFORE subtraction to prevent underflow
      int cost = (int)(litersDiff * config.pricePerLiter);

      // FIX: Always update totalDispensedLiters first
      totalDispensedLiters += litersDiff;

      if (cost >= balance) {
        // Balance depleted - FIX: Go to IDLE, not ACTIVE
        balance = 0;
        currentState = IDLE;
        setRelay(false);
        resetSessionTimer(); // Prevent stale lastSessionActivity

        publishLog("BALANCE", "Depleted");
        publishStatus();
      } else {
        // Normal deduction
        balance -= cost;
      }

    } else if (currentState == FREE_WATER) {
      freeWaterDispensed += litersDiff;

      if (freeWaterDispensed >= config.freeWaterAmount) {
        freeWaterUsed = true;
        freeWaterAvailableTime = millis() + config.freeWaterCooldown;

        if (balance > 0) {
          // Cash was inserted during free water - continue directly as paid
          // dispensing so relay stays ON and flow is billed immediately.
          currentState = DISPENSING;
          sessionStartBalance = balance;
          // Reset flow counters for paid dispensing
          flowPulseCount = 0;
          lastDispensedLiters = 0.0;
          totalDispensedLiters = 0.0;
          resetSessionTimer();
          Serial.println("💰 FREE_WATER → DISPENSING (balance available)");
          // Relay stays ON - water continues
        } else {
          // No balance - go back to idle
          currentState = IDLE;
          setRelay(false);
        }

        publishLog("FREE_WATER", "Completed");
        publishStatus();
      }
    }
  }
}
