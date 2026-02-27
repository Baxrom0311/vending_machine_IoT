#ifndef CASH_HANDLER_H
#define CASH_HANDLER_H

#include <Arduino.h>

// ============================================
// CONFIGURATION
// ============================================
#define CASH_PULSE_VALUE 500 // So'm per pulse (default)
#define CASH_PULSE_GAP_MS 600 // Minimum gap between pulse groups

// Pulse width filtering (noise protection)
#define CASH_MIN_PULSE_MS 10  // Ignore very short spikes
#define CASH_MAX_PULSE_MS 250 // Ignore abnormally long/stuck pulses

// Pulse active level:
// - LOW: most bill acceptors pull line to GND during pulse
// - HIGH: set this if your optocoupler inverts the signal
// - CASH_ACTIVE_LEVEL_AUTO: derive active level from idle pin state at boot
#define CASH_ACTIVE_LEVEL_AUTO -1
#define CASH_ACTIVE_LEVEL CASH_ACTIVE_LEVEL_AUTO

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

// Set pulse gap (ms) (can be updated via UART from main)
void setCashPulseGapMs(unsigned long gapMs);

#endif
