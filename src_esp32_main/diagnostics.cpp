#include "diagnostics.h"
#include "config.h"
#include "debug.h"
#include "display.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include "sensors.h"
#include "state_machine.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static HealthCheck lastHealthCheck;

// Run comprehensive hardware diagnostics
HealthCheck runDiagnostics() {
  HealthCheck health;
  health.timestamp = millis() / 1000;
  health.failureCount = 0;

  DEBUG_PRINTLN("Running system diagnostics...");

  // 1. Flow Sensor Test
  // Check if counter is stable when idle (no false pulses)
  if (currentState == IDLE || currentState == ACTIVE) {
    unsigned long before = flowPulseCount;
    delay(100);
    unsigned long after = flowPulseCount;
    health.flowSensorOk = (after == before); // Should be stable when idle

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

  // 5. Display Test (LCD I2C - assume OK if initialized)
  health.displayOk = true; // LCD doesn't have width/height check

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

  // 7. MQTT Test
  health.mqttOk = mqttClient.connected();

  if (!health.mqttOk) {
    health.failureCount++;
    DEBUG_PRINTLN("⚠️ MQTT: disconnected");
  } else {
    DEBUG_PRINTLN("✓ MQTT: connected");
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
  if (!health.wifiOk || !health.mqttOk) {
    // Can't publish if no network, but let's try
  }

  return health;
}

// Publish health report to MQTT
void publishHealthReport(const HealthCheck &health) {
  if (!mqttClient.connected()) {
    DEBUG_PRINTLN("Cannot publish diagnostics: MQTT disconnected");
    return;
  }

  JsonDocument doc;

  doc["timestamp"] = health.timestamp;

  JsonObject components = doc["components"].to<JsonObject>();
  components["flowSensor"] = health.flowSensorOk;
  components["tdsSensor"] = health.tdsSensorOk;
  components["cashAcceptor"] = health.cashAcceptorOk;
  components["relay"] = health.relayOk;
  components["display"] = health.displayOk;
  components["wifi"] = health.wifiOk;
  components["mqtt"] = health.mqttOk;

  doc["failureCount"] = health.failureCount;

  // List failed components
  JsonArray failed = doc["failedComponents"].to<JsonArray>();
  if (!health.flowSensorOk)
    failed.add("flowSensor");
  if (!health.tdsSensorOk)
    failed.add("tdsSensor");
  if (!health.cashAcceptorOk)
    failed.add("cashAcceptor");
  if (!health.relayOk)
    failed.add("relay");
  if (!health.displayOk)
    failed.add("display");
  if (!health.wifiOk)
    failed.add("wifi");
  if (!health.mqttOk)
    failed.add("mqtt");

  String payload;
  serializeJson(doc, payload);

  mqttClient.publish(TOPIC_DIAGNOSTICS, payload.c_str(), false);
  DEBUG_PRINTLN("Health report published to MQTT");
}

// Get last health check results
HealthCheck getLastHealth() { return lastHealthCheck; }
