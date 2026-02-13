#include "cash_handler.h"
#include "hardware.h"

// ============================================
// VARIABLES
// ============================================
static volatile unsigned long pulseCount = 0;
static volatile unsigned long lastPulseMs = 0;
static int cashPulseValue = CASH_PULSE_VALUE;
static int pendingPayment = 0;

// ============================================
// ISR - Interrupt Service Routine
// ============================================
void IRAM_ATTR cashPulseISR() {
  unsigned long now = millis();

  // Hardware debounce
  if (now - lastPulseMs > CASH_DEBOUNCE_MS) {
    pulseCount++;
    lastPulseMs = now;
  }
}

// ============================================
// INITIALIZATION
// ============================================
void initCashHandler() {
  pinMode(CASH_PULSE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CASH_PULSE_PIN), cashPulseISR, FALLING);

  Serial.print("✓ Cash handler initialized on GPIO ");
  Serial.println(CASH_PULSE_PIN);
}

// ============================================
// PROCESS PULSES
// ============================================
void processCashPulses() {
  // Check if we have pulses and enough time passed
  unsigned long now = millis();

  noInterrupts();
  unsigned long pulses = pulseCount;
  unsigned long lastMs = lastPulseMs;
  interrupts();

  if (pulses == 0) {
    return;
  }

  // Wait for pulse gap (all pulses received)
  if (now - lastMs < CASH_PULSE_GAP_MS) {
    return;
  }

  // Clear counter
  noInterrupts();
  pulseCount = 0;
  interrupts();

  // Calculate payment
  int amount = (int)pulses * cashPulseValue / 2;
  pendingPayment += amount;

  Serial.print("💵 Cash received: ");
  Serial.print(amount);
  Serial.print(" so'm (");
  Serial.print(pulses);
  Serial.println(" pulses)");

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
