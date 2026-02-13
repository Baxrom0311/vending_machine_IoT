#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// ============================================
// ESP32 #1 - PAYMENT CONTROLLER
// ============================================
// Bu ESP32 faqat cash acceptor bilan ishlaydi
// va UART orqali Main ESP32 ga xabar yuboradi
//

// ============================================
// STATUS LED
// ============================================
#define LED_PIN 2 // Built-in LED for status

// ============================================
// CASH ACCEPTOR
// ============================================
#define CASH_PULSE_PIN 32 // Genius 7 pulse input
     // INPUT_PULLUP, FALLING edge

// ============================================
// UART (to Main ESP32)
// ============================================
#define UART_TX_PIN 17 // TX → Main ESP32 RX (GPIO 16)
#define UART_RX_PIN 16 // RX ← Main ESP32 TX (GPIO 17)

#endif
