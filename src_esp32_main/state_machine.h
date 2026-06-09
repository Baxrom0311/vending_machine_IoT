#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>

// ============================================
// SYSTEM STATE MACHINE
// ============================================
enum SystemState {
  IDLE,       // Kutish, balans = 0
  ACTIVE,     // Balans > 0, tayyor
  DISPENSING, // Suv quyish
  PAUSED      // Pauza
};

// ============================================
// GLOBAL STATE
// ============================================
extern SystemState currentState;
extern volatile long balance;

// Timers
extern unsigned long lastSessionActivity;

// ============================================
// FUNCTIONS
// ============================================
void initStateMachine();
void handleStartButton();
void handlePauseButton();
void handleSessionTimeout();
void resetSessionTimer();
void applyConfigStateEffects();

#endif
