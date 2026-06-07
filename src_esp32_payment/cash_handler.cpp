#include "cash_handler.h"
#include "hardware.h"
#include "driver/gpio.h"
#include <climits>

#ifndef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 1
#endif

#if ENABLE_DEBUG_LOGS
#define PAY_CASH_LOG_PRINT(...) Serial.print(__VA_ARGS__)
#define PAY_CASH_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define PAY_CASH_LOG_PRINT(...)
#define PAY_CASH_LOG_PRINTLN(...)
#endif

// ============================================
// VARIABLES
// ============================================
static volatile unsigned long pulseCount = 0;
static volatile uint32_t lastPulseEndUs = 0;
static volatile uint32_t pulseStartUs = 0;
static volatile uint32_t lastPulseStartUs = 0;
static volatile bool pulseActive = false;
static int cashPulseValue = CASH_PULSE_VALUE;
static unsigned long cashPulseGapMs = CASH_PULSE_GAP_MS;
static int pendingPayment = 0;
static int activePulseLevel = LOW;
static constexpr uint32_t CASH_MIN_EDGE_GAP_US = 2000UL;
static constexpr uint32_t CASH_MIN_PULSE_INTERVAL_US = 5000UL; // 5ms; 25ms was too aggressive
static constexpr int VALID_BILL_DENOMS[] = {1000, 2000, 5000, 10000, 20000,
                                            50000, 100000};

static int normalizeBillAmount(int rawAmount) {
  if (rawAmount <= 0) {
    return rawAmount;
  }

  for (int denom : VALID_BILL_DENOMS) {
    if (rawAmount == denom) {
      return rawAmount;
    }

    // If the validator misses exactly one pulse, the raw amount lands just
    // below a real bill denomination. Round only that narrow case.
    if (rawAmount < denom && (denom - rawAmount) <= cashPulseValue) {
      return denom;
    }
  }

  return rawAmount;
}

// ============================================
// ISR - Interrupt Service Routine
// ============================================
void IRAM_ATTR cashPulseISR() {
  const uint32_t nowUs = micros();
  const int level = gpio_get_level(static_cast<gpio_num_t>(CASH_PULSE_PIN));

  if (level == activePulseLevel) {
    if (pulseActive) {
      return;
    }
    if ((uint32_t)(nowUs - lastPulseStartUs) < CASH_MIN_EDGE_GAP_US) {
      return;
    }
    pulseActive = true;
    pulseStartUs = nowUs;
    lastPulseStartUs = nowUs;
    return;
  }

  if (!pulseActive) {
    return;
  }

  pulseActive = false;
  const uint32_t widthMs = (nowUs - pulseStartUs) / 1000UL;
  if ((uint32_t)(nowUs - lastPulseEndUs) < CASH_MIN_PULSE_INTERVAL_US) {
    return;
  }
  if (widthMs >= CASH_MIN_PULSE_MS && widthMs <= CASH_MAX_PULSE_MS) {
    pulseCount++;
    lastPulseEndUs = nowUs;
  }
}

// ============================================
// INITIALIZATION
// ============================================
void initCashHandler() {
  pinMode(CASH_PULSE_PIN, INPUT_PULLUP);
  const int idleLevel = digitalRead(CASH_PULSE_PIN);

#if CASH_ACTIVE_LEVEL == CASH_ACTIVE_LEVEL_AUTO
  activePulseLevel = (idleLevel == HIGH) ? LOW : HIGH;
#else
  activePulseLevel = CASH_ACTIVE_LEVEL;
#endif

  pulseCount = 0;
  lastPulseEndUs = 0;
  pulseStartUs = 0;
  lastPulseStartUs = 0;
  pulseActive = false;

  attachInterrupt(digitalPinToInterrupt(CASH_PULSE_PIN), cashPulseISR, CHANGE);

  PAY_CASH_LOG_PRINT("✓ Cash handler initialized on GPIO ");
  PAY_CASH_LOG_PRINTLN(CASH_PULSE_PIN);
  PAY_CASH_LOG_PRINT("  Idle level: ");
  PAY_CASH_LOG_PRINTLN(idleLevel == LOW ? "LOW" : "HIGH");
  PAY_CASH_LOG_PRINT("  Active level: ");
  PAY_CASH_LOG_PRINTLN(activePulseLevel == LOW ? "LOW" : "HIGH");
  PAY_CASH_LOG_PRINT("  Pulse width range: ");
  PAY_CASH_LOG_PRINT(CASH_MIN_PULSE_MS);
  PAY_CASH_LOG_PRINT("..");
  PAY_CASH_LOG_PRINT(CASH_MAX_PULSE_MS);
  PAY_CASH_LOG_PRINTLN(" ms");
  PAY_CASH_LOG_PRINT("  Bill timeout: ");
  PAY_CASH_LOG_PRINT(CASH_PULSE_GAP_MS);
  PAY_CASH_LOG_PRINTLN(" ms");
  PAY_CASH_LOG_PRINT("  Pulse value: ");
  PAY_CASH_LOG_PRINT(cashPulseValue);
  PAY_CASH_LOG_PRINTLN(" so'm");
}

// ============================================
// PROCESS PULSES
// ============================================
void processCashPulses() {
  const uint32_t nowUs = micros();

  noInterrupts();
  const unsigned long pulsesSnapshot = pulseCount;
  const uint32_t lastPulseUs = lastPulseEndUs;
  interrupts();

  if (pulsesSnapshot == 0) {
    return;
  }

  const uint32_t gapUs = static_cast<uint32_t>(cashPulseGapMs * 1000UL);
  if ((uint32_t)(nowUs - lastPulseUs) <= gapUs) {
    return;
  }

  noInterrupts();
  const unsigned long pulses = pulseCount;
  pulseCount = 0;
  interrupts();

  const int rawAmount = (int)pulses * cashPulseValue;
  const int amount = normalizeBillAmount(rawAmount);
  if (amount > 0 && pendingPayment > INT_MAX - amount) {
    pendingPayment = INT_MAX;
    PAY_CASH_LOG_PRINTLN("⚠️ pendingPayment saturated at INT_MAX");
  } else {
    pendingPayment += amount;
  }

  PAY_CASH_LOG_PRINTLN("========================");
  PAY_CASH_LOG_PRINT("BILL_DONE pulses=");
  PAY_CASH_LOG_PRINTLN(pulses);
  if (amount != rawAmount) {
    PAY_CASH_LOG_PRINT("RAW_AMOUNT=");
    PAY_CASH_LOG_PRINTLN(rawAmount);
    PAY_CASH_LOG_PRINT("NORMALIZED_AMOUNT=");
    PAY_CASH_LOG_PRINTLN(amount);
  }
  PAY_CASH_LOG_PRINT("QABUL_QILINGAN_PUL=");
  PAY_CASH_LOG_PRINTLN(amount);
  PAY_CASH_LOG_PRINT("PENDING_PUL=");
  PAY_CASH_LOG_PRINTLN(pendingPayment);
  PAY_CASH_LOG_PRINTLN("========================");
}
// ============================================
// GETTERS/SETTERS
// ============================================
int getPendingPayment() { return pendingPayment; }
void clearPendingPayment() { pendingPayment = 0; }
void setCashPulseValue(int value) {
  if (value > 0 && value <= 1000000) {
    cashPulseValue = value;
    PAY_CASH_LOG_PRINT("Cash pulse value set to: ");
    PAY_CASH_LOG_PRINTLN(value);
  }
}
void setCashPulseGapMs(unsigned long gapMs) {
  if (gapMs >= 20 && gapMs <= 1000) {
    cashPulseGapMs = gapMs;
    PAY_CASH_LOG_PRINT("Cash pulse gap set to: ");
    PAY_CASH_LOG_PRINTLN(gapMs);
  }
}
