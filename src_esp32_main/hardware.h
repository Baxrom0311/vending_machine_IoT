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
// I2C LCD DISPLAY (20x4 with PCF8574 backpack)
// User wiring:
//   SDA=GPIO21, SCL=GPIO22, Relay=GPIO19, TDS=GPIO34, Flow=GPIO32
//   Start=GPIO25, Pause=GPIO26
// ============================================
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x27
#endif

#ifndef LCD_I2C_ADDR_FALLBACK
#define LCD_I2C_ADDR_FALLBACK 0x3F
#endif

#ifndef LCD_I2C_CLOCK_HZ
#define LCD_I2C_CLOCK_HZ 100000UL
#endif

#ifndef LCD_COLS
#define LCD_COLS 20
#endif

#ifndef LCD_ROWS
#define LCD_ROWS 4
#endif

// ============================================
// SENSORS
// ============================================
#ifndef TDS_PIN
#define TDS_PIN 34 // Analog TDS sensor (ADC1_CH6, input-only)
#endif

#ifndef FLOW_SENSOR_PIN
#define FLOW_SENSOR_PIN 32 // Flow sensor pulse (ADC1_CH4, interrupt capable)
#endif
// 5V signal bo'lsa, 3.3V ga moslash shart!

// ============================================
// CONTROLS
// ============================================
#ifndef RELAY_PIN
#define RELAY_PIN 19 // Solenoid valve relay
#endif

#ifndef START_BUTTON_PIN
#define START_BUTTON_PIN 25 // Start/Resume button (INPUT_PULLUP)
#endif

#ifndef PAUSE_BUTTON_PIN
#define PAUSE_BUTTON_PIN 26 // Pause button (INPUT_PULLUP)
#endif

#if I2C_SDA_PIN == RELAY_PIN || I2C_SCL_PIN == RELAY_PIN
#warning "I2C pins conflict with RELAY_PIN. Rewire one of them."
#endif

#if I2C_SDA_PIN == FLOW_SENSOR_PIN || I2C_SCL_PIN == FLOW_SENSOR_PIN
#warning "I2C pins conflict with FLOW_SENSOR_PIN. Rewire one of them."
#endif

// ============================================
// UART (from Payment ESP32)
// ============================================
#ifndef UART_RX_PIN
#define UART_RX_PIN 16 // RX <- Payment ESP32 TX (GPIO 17)
#endif

#ifndef UART_TX_PIN
#define UART_TX_PIN 17 // TX -> Payment ESP32 RX (GPIO 16)
#endif

#endif
