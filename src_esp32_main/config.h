#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================
// CONFIGURATION STRUCTURE
// ============================================
struct Config {
  int pricePerLiter = 500;               // so'm
  unsigned long sessionTimeout = 300000; // 5 min (ms)
  float pulsesPerLiter = 450.0;          // Flow sensor calibration
  int tdsThreshold = 100;                // ppm - warning only
  float tdsTemperatureC = 25.0;          // TDS compensation temp
  float tdsCalibrationFactor = 0.5;      // TDS calibration multiplier
  bool relayActiveHigh = true;         // Relay polarity (default Active HIGH)
  int cashPulseValue = 1000;          // so'm per pulse
  unsigned long cashPulseGapMs = 600; // gap to close pulse burst
  unsigned long paymentCheckInterval = 2000; // 2s
  unsigned long displayUpdateInterval = 100; // 100ms
  unsigned long tdsCheckInterval = 5000;     // 5s
  unsigned long heartbeatInterval = 30000;   // 30s
  // Power Management removed
};

// ============================================
// GLOBAL CONFIG INSTANCE
// ============================================
extern Config config;

// ============================================
// FUNCTIONS
// ============================================
void setupWiFi();
void processWiFi();
void initConfig();
void applyRuntimeConfig();

#endif
