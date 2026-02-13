#ifndef UART_SENDER_H
#define UART_SENDER_H

#include <Arduino.h>

// ============================================
// FUNCTIONS
// ============================================

// Initialize UART communication
void initUartSender();

// Send payment to Main ESP32
// Returns true if delivered or queued for retry (false only if offline buffer
// is full and the payment could not be queued).
bool sendPayment(int amount);

// Send heartbeat to Main ESP32
void sendHeartbeat();

// Process incoming messages from Main ESP32
void processUartReceive();

// Check if Main ESP32 is connected
bool isMainEspConnected();

#endif
