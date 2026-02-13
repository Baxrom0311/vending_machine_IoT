#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>

// Health check result structure
struct HealthCheck {
  bool flowSensorOk;
  bool tdsSensorOk;
  bool cashAcceptorOk;
  bool relayOk;
  bool displayOk;
  bool wifiOk;
  bool mqttOk;
  uint32_t timestamp;
  int failureCount;
};

// Main diagnostics functions
HealthCheck runDiagnostics();
void publishHealthReport(const HealthCheck &health);
HealthCheck getLastHealth();

#endif
