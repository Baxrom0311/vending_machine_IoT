#include "state_machine.h"
#include "config.h"
#include "debug.h"
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

unsigned long lastSessionActivity = 0;
static SystemState pausedFromState = IDLE;
static float pendingDispenseCost = 0.0f;
static unsigned long lastBusyStartHintMs = 0;

static bool isDispenseBillingState(SystemState state) {
  return state == DISPENSING || state == PAUSED;
}

static void applyDispenseBilling(float litersDiff) {
  if (litersDiff <= 0.0f) {
    return;
  }

  // Keep billing late tail-flow pulses after PAUSE so residual water is not free.
  pendingDispenseCost += litersDiff * static_cast<float>(config.pricePerLiter);
  int cost = static_cast<int>(pendingDispenseCost);
  pendingDispenseCost -= static_cast<float>(cost);

  totalDispensedLiters += litersDiff;

  if (cost >= balance) {
    balance = 0;
    currentState = IDLE;
    pausedFromState = IDLE;
    pendingDispenseCost = 0.0f;
    setRelay(false);
    resetSessionTimer();

    publishLog("BALANCE", "Depleted");
    publishStatus();
    return;
  }

  balance -= cost;
}

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
  lastSessionActivity = millis();
  pausedFromState = IDLE;
  pendingDispenseCost = 0.0f;
  lastBusyStartHintMs = 0;
}

// ============================================
// SESSION TIMER
// ============================================
void resetSessionTimer() { lastSessionActivity = millis(); }

void resetFlowCounters() {
  noInterrupts();
  flowPulseCount = 0;
  interrupts();
  lastDispensedLiters = 0.0f;
}

// ============================================
// APPLY CONFIG EFFECTS (Runtime)
// ============================================
void applyConfigStateEffects() {
  if (balance <= 0) {
    currentState = IDLE;
    pausedFromState = IDLE;
    pendingDispenseCost = 0.0f;
    setRelay(false);
    return;
  }

  if (currentState == IDLE) {
    currentState = ACTIVE;
  }

  if (currentState == DISPENSING) {
    setRelay(true);
    return;
  }

  setRelay(false);
}

// ============================================
// SESSION TIMEOUT HANDLER
// ============================================
void handleSessionTimeout() {
  DEBUG_PRINTLN("Session timeout!");

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
  pendingDispenseCost = 0.0f;

  setRelay(false); // Close valve

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
      resetFlowCounters();
      totalDispensedLiters = 0.0f;
      sessionStartBalance = balance;
      pendingDispenseCost = 0.0f;
      setRelay(true);

      // Dispense started
      publishLog("DISPENSE", "Started");
      publishStatus();
    } else {
      // Feedback for user why start didn't work
      showTemporaryMessage("PUL KIRITING", "");
    }
    break;

  case ACTIVE:
    if (balance > 0) {
      currentState = DISPENSING;
      resetFlowCounters();
      totalDispensedLiters = 0.0f;
      sessionStartBalance = balance;
      pendingDispenseCost = 0.0f;
      setRelay(true);

      // Dispense started
      publishLog("DISPENSE", "Started");
      publishStatus();
    }
    break;

  case PAUSED:
    if (balance > 0) {
      currentState = DISPENSING;
      resetFlowCounters();
      pendingDispenseCost = 0.0f;
      setRelay(true);

      pausedFromState = IDLE;
      publishLog("DISPENSE", "Resumed");
      publishStatus();
    } else {
      showTemporaryMessage("PUL KIRITING", "Yoki kuting...");
    }
    break;

  case DISPENSING:
    // START is intentionally ignored while water is already running.
    // Throttle this hint to avoid I2C spam when user taps rapidly.
    if (millis() - lastBusyStartHintMs >= 1500UL) {
      lastBusyStartHintMs = millis();
      showTemporaryMessage("PAUSE=STOP", "");
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

  if (currentState == DISPENSING) {
    const SystemState prevState = currentState;
    pausedFromState = prevState;
    currentState = PAUSED;
    setRelay(false);

    DEBUG_PRINTLN("PAUSE button pressed - Relay OFF");
    char msg[32];
    snprintf(msg, sizeof(msg), "%.2f", totalDispensedLiters);
    publishLog("PAUSE", msg);
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
    DEBUG_PRINTLN("⚠️ Flow counter reset (normal overflow prevention)");
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

    if (isDispenseBillingState(currentState)) {
      applyDispenseBilling(litersDiff);
    }
  }
}
