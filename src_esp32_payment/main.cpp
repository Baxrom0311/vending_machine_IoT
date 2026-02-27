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

#ifndef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 1
#endif

#if ENABLE_DEBUG_LOGS
#define PAY_MAIN_LOG_PRINT(...) Serial.print(__VA_ARGS__)
#define PAY_MAIN_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define PAY_MAIN_LOG_PRINT(...)
#define PAY_MAIN_LOG_PRINTLN(...)
#endif

// ============================================
// CONFIGURATION
// ============================================
#define LOOP_DELAY_MS 1

// ============================================
// SETUP
// ============================================
void setup() {
  // Serial for debugging
  Serial.begin(115200);
  delay(1000);

  PAY_MAIN_LOG_PRINTLN();
  PAY_MAIN_LOG_PRINTLN("========================================");
  PAY_MAIN_LOG_PRINTLN("  eWater - ESP32 Payment Controller");
  PAY_MAIN_LOG_PRINTLN("  DEBUG MODE ENABLED");
  PAY_MAIN_LOG_PRINTLN("========================================");

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ============================================
  // HARDWARE WATCHDOG TIMER
  // ============================================
  PAY_MAIN_LOG_PRINTLN("Enabling Hardware Watchdog...");
  esp_task_wdt_init(30, true); // 30 second timeout, auto-reboot enabled
  esp_task_wdt_add(NULL);      // Add current task to watchdog
  PAY_MAIN_LOG_PRINTLN("✓ Watchdog enabled");

  // Initialize UART to Main ESP32
  initUartSender();

  // Initialize Cash Handler
  initCashHandler();

  // Check Pulse Pin State
  int pinState = digitalRead(CASH_PULSE_PIN);
  PAY_MAIN_LOG_PRINT("Initial CASH_PULSE_PIN (GPIO ");
  PAY_MAIN_LOG_PRINT(CASH_PULSE_PIN);
  PAY_MAIN_LOG_PRINT(") state: ");
  PAY_MAIN_LOG_PRINTLN(pinState == HIGH ? "HIGH (Normal for Pullup)"
                                        : "LOW (Warning: Start Active?)");

  PAY_MAIN_LOG_PRINTLN();
  PAY_MAIN_LOG_PRINTLN("✓ Payment Controller Ready!");
  PAY_MAIN_LOG_PRINTLN("  Waiting for cash...");
  PAY_MAIN_LOG_PRINTLN();

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
    PAY_MAIN_LOG_PRINT("💰 Pending Payment Detected: ");
    PAY_MAIN_LOG_PRINTLN(payment);

    // Send to Main ESP32
    bool sent = sendPayment(payment);

    if (sent) {
      clearPendingPayment();
      PAY_MAIN_LOG_PRINTLN("✅ Payment sent successfully!");

      // Blink LED to confirm
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
    } else {
      PAY_MAIN_LOG_PRINTLN("❌ Failed to send payment (Main ESP offline?)");
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
