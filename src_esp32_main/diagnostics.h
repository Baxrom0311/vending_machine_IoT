#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>

// Health check result structure
struct HealthCheck {
  bool cashAcceptorOk;
  bool relayOk;
  uint32_t timestamp;
  int failureCount;
};

// Main diagnostics functions
HealthCheck runDiagnostics();
void publishHealthReport(const HealthCheck &health);
HealthCheck getLastHealth();

#endif
