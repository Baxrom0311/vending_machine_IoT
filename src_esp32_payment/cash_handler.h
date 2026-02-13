#ifndef CASH_HANDLER_H
#define CASH_HANDLER_H

#include <Arduino.h>

// ============================================
// CONFIGURATION
// ============================================
#define CASH_PULSE_VALUE 1000 // So'm per pulse (default)
#define CASH_PULSE_GAP_MS 200 // Minimum gap between pulse groups
#define CASH_DEBOUNCE_MS 40   // Debounce time

// ============================================
// FUNCTIONS
// ============================================
void initCashHandler();
void processCashPulses();

// Get pending payment amount (0 if none)
int getPendingPayment();

// Clear pending payment after sending
void clearPendingPayment();

// Set pulse value (can be updated via UART from main)
void setCashPulseValue(int value);

#endif
