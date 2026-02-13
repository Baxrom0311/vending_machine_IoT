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
  PAUSED,     // Pauza
  FREE_WATER  // Bepul 200ml
};

// ============================================
// GLOBAL STATE
// ============================================
extern SystemState currentState;
extern volatile long balance;
extern float totalDispensedLiters;
extern float sessionStartBalance;

// Flow sensor
extern volatile unsigned long flowPulseCount;
extern float lastDispensedLiters;

// Free water
extern float freeWaterDispensed;
extern bool freeWaterUsed;

// Timers
extern unsigned long lastSessionActivity;
extern unsigned long freeWaterAvailableTime;

// ============================================
// FUNCTIONS
// ============================================
void initStateMachine();
void handleStartButton();
void handlePauseButton();
void handleSessionTimeout();
void processFlowSensor();
void resetSessionTimer();
void applyConfigStateEffects();

#endif
