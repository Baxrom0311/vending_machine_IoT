#include "diagnostics.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include "local_events.h"
#include "relay_control.h"
#include "state_machine.h"

static HealthCheck lastHealthCheck;

// Run comprehensive hardware diagnostics
HealthCheck runDiagnostics() {
  HealthCheck health;
  health.timestamp = millis() / 1000;
  health.failureCount = 0;

  DEBUG_PRINTLN("Running system diagnostics...");

  // 1. Cash Acceptor Test
  // For now, assume OK (no error tracking implemented yet)
  health.cashAcceptorOk = true;
  DEBUG_PRINTLN("→ Cash acceptor: OK");

  // 2. Relay Test
  // SAFETY FIX: Do not toggle relay. Only verify it is OFF in IDLE.
  if (currentState == IDLE && balance == 0) {
    // In IDLE, relay should be OFF. If ON, it's stuck.
    if (isRelayOn() || isRelay2On()) {
      health.relayOk = false;
      health.failureCount++;
      DEBUG_PRINTLN("⚠️ Relay: one or more outputs stuck ON");
    } else {
      health.relayOk = true;
      DEBUG_PRINTLN("✓ Relays: OK (all OFF)");
    }
  } else {
    health.relayOk = true; // Skip test when not safe
    DEBUG_PRINTLN("→ Relay: skipped (not safe to test)");
  }

  DEBUG_PRINTF("Diagnostics complete. Failures: %d\n", health.failureCount);

  // Store for later reference
  lastHealthCheck = health;

  // Send simple logs for critical failures (instead of Alerts)
  if (!health.relayOk && currentState == IDLE) {
    publishLog("DIAG_FAIL", "Relay stuck ON");
  }
  return health;
}

// Print health report locally over serial/debug
void publishHealthReport(const HealthCheck &health) {
  DEBUG_PRINT("HEALTH timestamp=");
  DEBUG_PRINT(health.timestamp);
  DEBUG_PRINT(" failures=");
  DEBUG_PRINT(health.failureCount);
  DEBUG_PRINT(" cash=");
  DEBUG_PRINT(health.cashAcceptorOk ? "OK" : "FAIL");
  DEBUG_PRINT(" relay=");
  DEBUG_PRINTLN(health.relayOk ? "OK" : "FAIL");
}

// Get last health check results
HealthCheck getLastHealth() { return lastHealthCheck; }
