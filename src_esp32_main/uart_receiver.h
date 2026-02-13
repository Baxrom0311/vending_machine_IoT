#ifndef UART_RECEIVER_H
#define UART_RECEIVER_H

#include <Arduino.h>

// ============================================
// FUNCTIONS
// ============================================

// Initialize UART communication with Payment ESP32
void initUartReceiver();

// Process incoming messages from Payment ESP32
// Call this in main loop
void processUartReceiver();

// Send ACK to Payment ESP32
void sendAck(uint32_t seq);

// Send status update to Payment ESP32
void sendStatusToPaymentEsp(const char *state, long balance);

// Check if Payment ESP32 is connected
bool isPaymentEspConnected();

#endif
