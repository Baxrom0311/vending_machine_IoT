#include "cash_handler.h"
#include "hardware.h"
#include "driver/gpio.h"

// ============================================
// VARIABLES
// ============================================
static volatile unsigned long pulseCount = 0;
static volatile uint32_t lastPulseEndUs = 0;
static volatile uint32_t pulseStartUs = 0;
static volatile bool pulseActive = false;
static int cashPulseValue = CASH_PULSE_VALUE;
static unsigned long cashPulseGapMs = CASH_PULSE_GAP_MS;
static int pendingPayment = 0;
static int activePulseLevel = LOW;

// ============================================
// ISR - Interrupt Service Routine
// ============================================
void IRAM_ATTR cashPulseISR() {
  const uint32_t nowUs = micros();
  const int level = gpio_get_level(static_cast<gpio_num_t>(CASH_PULSE_PIN));

  if (level == activePulseLevel) {
    pulseActive = true;
    pulseStartUs = nowUs;
    return;
  }

  if (!pulseActive) {
    return;
  }

  pulseActive = false;
  const uint32_t widthMs = (nowUs - pulseStartUs) / 1000UL;
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

  attachInterrupt(digitalPinToInterrupt(CASH_PULSE_PIN), cashPulseISR, CHANGE);

  Serial.print("✓ Cash handler initialized on GPIO ");
  Serial.println(CASH_PULSE_PIN);
  Serial.print("  Idle level: ");
  Serial.println(idleLevel == LOW ? "LOW" : "HIGH");
  Serial.print("  Active level: ");
  Serial.println(activePulseLevel == LOW ? "LOW" : "HIGH");
  Serial.print("  Pulse width range: ");
  Serial.print(CASH_MIN_PULSE_MS);
  Serial.print("..");
  Serial.print(CASH_MAX_PULSE_MS);
  Serial.println(" ms");
  Serial.print("  Bill timeout: ");
  Serial.print(CASH_PULSE_GAP_MS);
  Serial.println(" ms");
  Serial.print("  Pulse value: ");
  Serial.print(cashPulseValue);
  Serial.println(" so'm");
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

  int amount = (int)pulses * cashPulseValue;
  pendingPayment += amount;

  Serial.println("========================");
  Serial.print("BILL_DONE pulses=");
  Serial.println(pulses);
  Serial.print("QABUL_QILINGAN_PUL=");
  Serial.println(amount);
  Serial.print("PENDING_PUL=");
  Serial.println(pendingPayment);
  Serial.println("========================");
  // Blink LED
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
}
// ============================================
// GETTERS/SETTERS
// ============================================
int getPendingPayment() { return pendingPayment; }
void clearPendingPayment() { pendingPayment = 0; }
void setCashPulseValue(int value) {
  if (value > 0 && value <= 1000000) {
    cashPulseValue = value;
    Serial.print("Cash pulse value set to: ");
    Serial.println(value);
  }
}
void setCashPulseGapMs(unsigned long gapMs) {
  if (gapMs >= 20 && gapMs <= 1000) {
    cashPulseGapMs = gapMs;
    Serial.print("Cash pulse gap set to: ");
    Serial.println(gapMs);
  }
}
