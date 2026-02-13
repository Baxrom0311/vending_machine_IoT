#ifndef ANALYTICS_H
#define ANALYTICS_H

#include <ArduinoJson.h>

class Analytics {
public:
  void begin();
  void loop();
  void incrementMqttReconnects();
  void recordPayment(int amount);
  void incrementDispenses();
  void recordCashError();
  void recordFlowError();
  void recordTds(int val);
  void generateReport(JsonDocument &doc);
};

#endif

