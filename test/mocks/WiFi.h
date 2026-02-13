#ifndef WIFI_H
#define WIFI_H

#include "Arduino.h"
#include <stdint.h>

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

class IPAddress {
public:
  IPAddress() {}
  IPAddress(uint8_t, uint8_t, uint8_t, uint8_t) {}
};

class WiFiClass {
public:
  int status() { return WL_CONNECTED; }
  void begin(const char *ssid, const char *pass) {}
  IPAddress localIP() { return IPAddress(192, 168, 1, 100); }
};

extern WiFiClass WiFi;

class WiFiClient {
public:
  WiFiClient() {}
  bool connect(const char *host, uint16_t port) { return true; }
  bool connected() { return true; }
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t *buf, size_t size) { return size; }
  void stop() {}
};

#endif
