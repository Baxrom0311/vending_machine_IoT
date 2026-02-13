#include "analytics.h"
#include "../../src_esp32_main/hardware.h"
#include "../../src_esp32_main/mqtt_handler.h"
#include <iostream>

// ============================================
// ANALYTICS MOCK
// ============================================
__attribute__((weak)) Analytics analytics;

void Analytics::begin() {}
void Analytics::loop() {}
void Analytics::incrementMqttReconnects() {}
void Analytics::recordPayment(int amount) {}
void Analytics::incrementDispenses() {
  // std::cout << "MOCK: Analytics::incrementDispenses()" << std::endl;
}
void Analytics::recordCashError() {}
void Analytics::recordFlowError() {}
void Analytics::recordTds(int val) {}
void Analytics::generateReport(JsonDocument &doc) {}

// ============================================
// MQTT MOCK
// ============================================
// Real PubSubClient is mocked in PubSubClient.h
// Real mqtt_handler.cpp will be included, so we don't stub its functions here.

// ============================================
// CONFIG MOCK
// ============================================
#include "../../src_esp32_main/config.h"
__attribute__((weak)) Config config;

// Stubs for config.h functions
char TOPIC_PAYMENT_IN[64];
char TOPIC_STATUS_OUT[64];
char TOPIC_CONFIG_IN[64];
char TOPIC_LOG_OUT[64];
char TOPIC_TDS_OUT[64];
char TOPIC_HEARTBEAT[64];
char TOPIC_OTA_IN[64];
char TOPIC_TELEMETRY[64];
char TOPIC_ALERTS[64];
char TOPIC_DIAGNOSTICS[64];
char TOPIC_BROADCAST_CONFIG[64];
char TOPIC_BROADCAST_COMMAND[64];
char TOPIC_GROUP_CONFIG[64];
char TOPIC_GROUP_COMMAND[64];

char wifi_ssid[32];
char wifi_password[64];
char mqtt_broker[64];
int mqtt_port;
char device_id[32];
char admin_password[32];
char groupId[32];

void setupWiFi() {}
void processWiFi() {}
void initConfig() {}
void applyRuntimeConfig() {}
void generateMQTTTopics() {}
