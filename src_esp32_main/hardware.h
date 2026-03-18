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
// SPI TFT DISPLAY (2.0" 7-pin, ST7789-compatible)
// User wiring:
//   SDA(MOSI)=GPIO23, SCL(SCLK)=GPIO18, RST=GPIO33, DC=GPIO27, CS=GPIO5
//   Relay=GPIO19, TDS=GPIO34, Flow=GPIO32, Start=GPIO25, Pause=GPIO26
// ============================================
#ifndef TFT_MOSI_PIN
#define TFT_MOSI_PIN 23
#endif

#ifndef TFT_SCLK_PIN
#define TFT_SCLK_PIN 18
#endif

#ifndef TFT_CS_PIN
#define TFT_CS_PIN 5
#endif

#ifndef TFT_DC_PIN
#define TFT_DC_PIN 27
#endif

#ifndef TFT_RST_PIN
#define TFT_RST_PIN 33
#endif

#ifndef TFT_MISO_PIN
#define TFT_MISO_PIN -1
#endif

#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif

#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif

#ifndef TFT_ROTATION
#define TFT_ROTATION 1 // 1 = landscape
#endif

#ifndef TFT_TEXT_SIZE
#define TFT_TEXT_SIZE 3
#endif

#ifndef TFT_TOP_MARGIN_PX
#define TFT_TOP_MARGIN_PX 12
#endif

#ifndef TFT_LINE_SPACING_PX
#define TFT_LINE_SPACING_PX 6
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
#define RELAY_PIN 19 // Solenoid valve relay (ACTIVE_HIGH in firmware)
#endif

#ifndef START_BUTTON_PIN
#define START_BUTTON_PIN 25 // Start/Resume button (INPUT_PULLUP)
#endif

#ifndef PAUSE_BUTTON_PIN
#define PAUSE_BUTTON_PIN 26 // Pause button (INPUT_PULLUP)
#endif

#if TFT_SCLK_PIN == RELAY_PIN
#warning "TFT_SCLK_PIN conflicts with RELAY_PIN. Rewire one of them."
#endif

#if TFT_RST_PIN == TDS_PIN
#warning "TFT_RST_PIN conflicts with TDS_PIN. Rewire one of them."
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
