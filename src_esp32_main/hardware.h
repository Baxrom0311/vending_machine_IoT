#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// ============================================
// ESP32 #2 - MAIN CONTROLLER
// ============================================
// Bu ESP32 display, relay, sensors, WiFi/MQTT
// va UART orqali Payment ESP32 dan xabar oladi
//

// ============================================
// I2C LCD DISPLAY (20x4 @ 0x27)
// ============================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ============================================
// SENSORS
// ============================================
#define TDS_PIN 33 // Analog TDS sensor (ADC1_CH5)
#define FLOW_SENSOR_PIN                                                        \
  35 // Flow sensor pulse (ADC1_CH7, input only)
     // ⚠️ 5V signal → 3.3V voltage divider kerak!

// ============================================
// CONTROLS
// ============================================
#define RELAY_PIN 18        // Solenoid valve relay (ACTIVE_HIGH in firmware)
#define START_BUTTON_PIN 25 // Start/Resume button (INPUT_PULLUP)
#define PAUSE_BUTTON_PIN 26 // Pause button (INPUT_PULLUP)

// ============================================
// UART (from Payment ESP32)
// ============================================
#define UART_RX_PIN 16 // RX ← Payment ESP32 TX (GPIO 17)
#define UART_TX_PIN 17 // TX → Payment ESP32 RX (GPIO 16)

#endif
