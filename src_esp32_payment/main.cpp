/*
 * ========================================
 * ESP32 #1 - PAYMENT CONTROLLER (DEBUG MODE)
 * ========================================
 */

#include "cash_handler.h"
#include "hardware.h"
#include "uart_sender.h"
#include <Arduino.h>
#include <esp_task_wdt.h>

// ============================================
// CONFIGURATION
// ============================================
#define LOOP_DELAY_MS 10

// ============================================
// SETUP
// ============================================
void setup() {
  // Serial for debugging
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("  eWater - ESP32 Payment Controller");
  Serial.println("  DEBUG MODE ENABLED");
  Serial.println("========================================");

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ============================================
  // HARDWARE WATCHDOG TIMER
  // ============================================
  Serial.println("Enabling Hardware Watchdog...");
  esp_task_wdt_init(30, true); // 30 second timeout, auto-reboot enabled
  esp_task_wdt_add(NULL);      // Add current task to watchdog
  Serial.println("✓ Watchdog enabled");

  // Initialize UART to Main ESP32
  initUartSender();

  // Initialize Cash Handler
  initCashHandler();

  // Check Pulse Pin State
  int pinState = digitalRead(CASH_PULSE_PIN);
  Serial.print("Initial CASH_PULSE_PIN (GPIO ");
  Serial.print(CASH_PULSE_PIN);
  Serial.print(") state: ");
  Serial.println(pinState == HIGH ? "HIGH (Normal for Pullup)"
                                  : "LOW (Warning: Start Active?)");

  Serial.println();
  Serial.println("✓ Payment Controller Ready!");
  Serial.println("  Waiting for cash...");
  Serial.println();

  // Blink LED 3 times to indicate ready
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  // Reset watchdog timer - "I'm alive!"
  esp_task_wdt_reset();

  // Process cash pulses
  processCashPulses();

  // Check for pending payment
  int payment = getPendingPayment();
  if (payment > 0) {
    Serial.print("💰 Pending Payment Detected: ");
    Serial.println(payment);

    // Send to Main ESP32
    bool sent = sendPayment(payment);

    if (sent) {
      clearPendingPayment();
      Serial.println("✅ Payment sent successfully!");

      // Blink LED to confirm
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
    } else {
      Serial.println("❌ Failed to send payment (Main ESP offline?)");
    }
  }

  // Send heartbeat periodically
  static unsigned long lastHb = 0;
  if (millis() - lastHb > 2000) { // More frequent heartbeat for debug
    lastHb = millis();
    // Serial.println("💓 Sending heartbeat..."); // Reduce spam
    sendHeartbeat();
  }

  // Process incoming UART messages
  processUartReceive();

  // Status LED - solid if connected, blink if offline
  static unsigned long lastBlinkMs = 0;
  if (!isMainEspConnected()) {
    if (millis() - lastBlinkMs > 1000) {
      lastBlinkMs = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  } else {
    digitalWrite(LED_PIN, HIGH); // Solid ON if connected
  }

  delay(LOOP_DELAY_MS);
}
