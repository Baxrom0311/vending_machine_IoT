#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "config_storage.h"

// ============================================
// MQTT CLIENT
// ============================================
extern PubSubClient mqttClient;

// ============================================
// FUNCTIONS
// ============================================
void setupMQTT();
void reconnectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void handleConfigUpdate(JsonDocument &doc);
void processPayment(int amount, const char *source, const char *txnId,
                    const char *userId);
void publishStatus();
void publishLog(const char *event, const char *message);
void publishMQTT(const char *topic, const char *message);
void beginNetworkApply(const DeviceConfig &previous, bool wifiChanged,
                       bool mqttChanged);
void processNetworkApply();

#endif
