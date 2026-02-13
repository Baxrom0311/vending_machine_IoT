#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include <Arduino.h>

// ============================================
// SERIAL CONFIGURATION PROTOCOL
// ============================================
/*
 * Commands:
 * - GET_CONFIG                    → Show all configuration
 * - SET_WIFI:ssid:password        → Set WiFi credentials
 * - SET_MQTT:broker:port          → Set MQTT broker
 * - SET_MQTT_AUTH:user:pass       → Set MQTT auth
 * - SET_DEVICE_ID:name            → Set device ID
 * - SET_PRICE:amount              → Set price per liter
 * - SET_TIMEOUT:seconds           → Set session timeout
 * - SET_FREE_WATER:1|0            → Enable/disable free water
 * - SET_CASH_PULSE:value          → Cash acceptor: so'm per pulse
 * - SET_CASH_GAP:ms               → Cash acceptor: pulse gap (ms)
 * - SAVE_CONFIG                   → Save current config to EEPROM
 * - LOAD_CONFIG                   → Reload config from EEPROM
 * - FACTORY_RESET                 → Reset to defaults
 * - GET_STATUS                    → Show device status
 * - RESTART                       → Restart device
 * - HELP                          → Show available commands
 */

// ============================================
// FUNCTIONS
// ============================================
void initSerialConfig();
void handleSerialConfig();
void processCommand(String cmd);
void showHelp();
void showStatus();

#endif
