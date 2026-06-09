#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// ============================================
// ESP32 #2 - MAIN CONTROLLER
// ============================================
// Bu ESP32 relay va UART orqali Payment ESP32 dan xabar oladi.
//

// ============================================
// CONTROLS
// ============================================
#ifndef RELAY_PIN
#define RELAY_PIN 19 // Solenoid valve relay (ACTIVE_HIGH in firmware)
#endif

#ifndef RELAY2_PIN
#define RELAY2_PIN 18 // Secondary pump relay
#endif

#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 21
#endif

#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 22
#endif

#ifndef START_BUTTON_PIN
#define START_BUTTON_PIN 25 // Start/Resume button (INPUT_PULLUP)
#endif

#ifndef PAUSE_BUTTON_PIN
#define PAUSE_BUTTON_PIN 26 // Pause button (INPUT_PULLUP)
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
