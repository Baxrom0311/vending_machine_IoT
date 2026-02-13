#ifndef PUBSUBCLIENT_H
#define PUBSUBCLIENT_H

#include "Arduino.h"
#include <stdint.h>
#include <string>

class WiFiClient;

class PubSubClient {
public:
  PubSubClient() {}
  PubSubClient(void *client) {}
  PubSubClient(WiFiClient &client) {} // Added constructor

  PubSubClient &setServer(const char *domain, uint16_t port) { return *this; }
  PubSubClient &setCallback(void (*callback)(char *, uint8_t *, unsigned int)) {
    return *this;
  }
  PubSubClient &setClient(void *client) { return *this; }
  PubSubClient &setBufferSize(uint16_t size) { return *this; }
  PubSubClient &setKeepAlive(uint16_t seconds) { return *this; }
  PubSubClient &setSocketTimeout(uint16_t seconds) { return *this; }

  bool connect(const char *id) { return true; }
  bool connect(const char *id, const char *user, const char *pass) {
    return true;
  }
  bool connect(const char *id, const char *willTopic, uint8_t willQos,
               boolean willRetain, const char *willMessage) {
    return true;
  }

  void disconnect() {}
  bool publish(const char *topic, const char *payload) { return true; }
  bool publish(const char *topic, const char *payload, boolean retained) {
    return true;
  }
  bool subscribe(const char *topic) { return true; }
  bool subscribe(const char *topic, uint8_t qos) { return true; }
  bool unsubscribe(const char *topic) { return true; }
  bool loop() { return true; }
  bool connected() { return true; }
  int state() { return 0; }
};

#endif
