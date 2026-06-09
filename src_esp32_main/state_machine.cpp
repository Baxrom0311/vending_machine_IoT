#include "state_machine.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include "local_events.h"
#include "relay_control.h"

// Note: lastSessionActivity is declared extern in state_machine.h

// ============================================
// GLOBAL STATE VARIABLES
// ============================================
SystemState currentState = IDLE;
volatile long balance = 0;

unsigned long lastSessionActivity = 0;
static unsigned long lastBusyStartHintMs = 0;

// ============================================
// INITIALIZATION
// ============================================
void initStateMachine() {
  currentState = IDLE;
  balance = 0;
  lastSessionActivity = millis();
  lastBusyStartHintMs = 0;
}

// ============================================
// SESSION TIMER
// ============================================
void resetSessionTimer() { lastSessionActivity = millis(); }

// ============================================
// APPLY CONFIG EFFECTS (Runtime)
// ============================================
void applyConfigStateEffects() {}

// ============================================
// SESSION TIMEOUT HANDLER
// ============================================
void handleSessionTimeout() {
  DEBUG_PRINTLN("Session timeout!");

  if (balance > 0) {
    publishLog("TIMEOUT", "Session expired, balance cleared");
  }

  balance = 0;
  currentState = IDLE;
  setDispenseOutputsActive(false);

  publishStatus();
}

// ============================================
// START BUTTON HANDLER
// ============================================
void handleStartButton() {
  resetSessionTimer();

  switch (currentState) {
  case IDLE:
  case ACTIVE:
    if (balance <= 0) {
      DEBUG_PRINTLN("START ignored: balance is zero");
      break;
    }
    currentState = DISPENSING;
    setDispenseOutputsActive(true);
    publishLog("DISPENSE", "Started");
    publishStatus();
    break;

  case PAUSED:
    if (balance > 0) {
      currentState = DISPENSING;
      setDispenseOutputsActive(true);
      publishLog("DISPENSE", "Resumed");
      publishStatus();
    } else {
      DEBUG_PRINTLN("RESUME ignored: balance is zero");
    }
    break;

  case DISPENSING:
    // START is intentionally ignored while water is already running.
    if (millis() - lastBusyStartHintMs >= 1500UL) {
      lastBusyStartHintMs = millis();
      DEBUG_PRINTLN("START ignored: dispensing already active");
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
  if (balance <= 0) {
    DEBUG_PRINTLN("PAUSE ignored: balance is zero");
    return;
  }

  resetSessionTimer();

  if (currentState == DISPENSING) {
    currentState = PAUSED;
    setDispenseOutputsActive(false);
    DEBUG_PRINTLN("PAUSE button pressed - Relay OFF");
    publishLog("PAUSE", "Paused");
    publishStatus();
  }
}
