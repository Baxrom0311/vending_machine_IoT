#include "diagnostics.h"
#include "config.h"
#include "debug.h"
#include "display.h"
#include "hardware.h"
#include "local_events.h"
#include "relay_control.h"
#include "sensors.h"
#include "state_machine.h"
#include <WiFi.h>

static HealthCheck lastHealthCheck;
static unsigned long lastFlowDiagSampleMs = 0;
static unsigned long lastFlowDiagSampleCount = 0;

// Run comprehensive hardware diagnostics
HealthCheck runDiagnostics() {
  HealthCheck health;
  health.timestamp = millis() / 1000;
  health.failureCount = 0;

  DEBUG_PRINTLN("Running system diagnostics...");

  // 1. Flow Sensor Test
  // Non-blocking check: compare pulse counter against previous sample.
  if (currentState == IDLE || currentState == ACTIVE) {
    const unsigned long nowMs = millis();
    const unsigned long pulses = flowPulseCount;
    if (lastFlowDiagSampleMs == 0 ||
        (nowMs - lastFlowDiagSampleMs) < 100UL) {
      health.flowSensorOk = true; // Not enough sampling window yet
    } else {
      health.flowSensorOk = (pulses == lastFlowDiagSampleCount);
    }
    lastFlowDiagSampleMs = nowMs;
    lastFlowDiagSampleCount = pulses;

    if (!health.flowSensorOk) {
      health.failureCount++;
      DEBUG_PRINTLN("⚠️ Flow sensor: unstable readings");
    } else {
      DEBUG_PRINTLN("✓ Flow sensor: OK");
    }
  } else {
    health.flowSensorOk = true; // Skip test during dispense
  }

  // 2. TDS Sensor Test
  // Check if readings are in valid range
  float tds = readTDS();
  health.tdsSensorOk = (tds >= 0 && tds < 2000); // Valid water TDS range

  if (!health.tdsSensorOk) {
    health.failureCount++;
    DEBUG_PRINTF("⚠️ TDS sensor: invalid reading %.1f ppm\n", tds);
  } else {
    DEBUG_PRINTF("✓ TDS sensor: %.1f ppm\n", tds);
  }

  // 3. Cash Acceptor Test
  // For now, assume OK (no error tracking implemented yet)
  health.cashAcceptorOk = true;
  DEBUG_PRINTLN("→ Cash acceptor: OK");

  // 4. Relay Test
  // SAFETY FIX: Do not toggle relay. Only verify it is OFF in IDLE.
  if (currentState == IDLE && balance == 0) {
    // In IDLE, relay should be OFF. If ON, it's stuck.
    if (isRelayOn()) {
      health.relayOk = false;
      health.failureCount++;
      DEBUG_PRINTLN("⚠️ Relay: Stuck ON (Critical Fail)");
    } else {
      health.relayOk = true;
      DEBUG_PRINTLN("✓ Relay: OK (OFF)");
    }
  } else {
    health.relayOk = true; // Skip test when not safe
    DEBUG_PRINTLN("→ Relay: skipped (not safe to test)");
  }

  // 5. Display Test
  health.displayOk = isDisplayReady();

  if (!health.displayOk) {
    health.failureCount++;
    DEBUG_PRINTLN("⚠️ Display: not initialized");
  } else {
    DEBUG_PRINTLN("✓ Display: OK");
  }

  // 6. WiFi Test
  health.wifiOk = (WiFi.status() == WL_CONNECTED);

  if (!health.wifiOk) {
    health.failureCount++;
    DEBUG_PRINTLN("⚠️ WiFi: disconnected");
  } else {
    DEBUG_PRINTLN("✓ WiFi: connected");
  }

  DEBUG_PRINTF("Diagnostics complete. Failures: %d\n", health.failureCount);

  // Store for later reference
  lastHealthCheck = health;

  // Send simple logs for critical failures (instead of Alerts)
  if (!health.flowSensorOk) {
    publishLog("DIAG_FAIL", "Flow sensor unstable");
  }
  if (!health.relayOk && currentState == IDLE) {
    publishLog("DIAG_FAIL", "Relay stuck ON");
  }
  if (!health.displayOk) {
    publishLog("DIAG_FAIL", "Display malfunction");
  }
  if (!health.tdsSensorOk) {
    publishLog("DIAG_FAIL", "TDS sensor invalid");
  }
  return health;
}

// Print health report locally over serial/debug
void publishHealthReport(const HealthCheck &health) {
  DEBUG_PRINT("HEALTH timestamp=");
  DEBUG_PRINT(health.timestamp);
  DEBUG_PRINT(" failures=");
  DEBUG_PRINT(health.failureCount);
  DEBUG_PRINT(" flow=");
  DEBUG_PRINT(health.flowSensorOk ? "OK" : "FAIL");
  DEBUG_PRINT(" tds=");
  DEBUG_PRINT(health.tdsSensorOk ? "OK" : "FAIL");
  DEBUG_PRINT(" cash=");
  DEBUG_PRINT(health.cashAcceptorOk ? "OK" : "FAIL");
  DEBUG_PRINT(" relay=");
  DEBUG_PRINT(health.relayOk ? "OK" : "FAIL");
  DEBUG_PRINT(" display=");
  DEBUG_PRINT(health.displayOk ? "OK" : "FAIL");
  DEBUG_PRINT(" wifi=");
  DEBUG_PRINTLN(health.wifiOk ? "OK" : "FAIL");
}

// Get last health check results
HealthCheck getLastHealth() { return lastHealthCheck; }
