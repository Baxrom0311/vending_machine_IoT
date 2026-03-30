#include "relay_control.h"
#include "config.h"
#include "debug.h"
#include "display.h"
#include "hardware.h"
#include <Arduino.h>

static int relayOnLevel() { return config.relayActiveHigh ? HIGH : LOW; }

static int relayOffLevel() { return config.relayActiveHigh ? LOW : HIGH; }
static volatile unsigned long relayLastChangeUs = 0;

void setRelay(bool on) {
  int level = on ? relayOnLevel() : relayOffLevel();
  digitalWrite(RELAY_PIN, level);
  relayLastChangeUs = micros();
  // Relay edges are the most likely EMI source for TFT corruption.
  // Re-init the panel after a short quiet period so the UI recovers if the
  // controller reset/desynced while ESP32 kept running.
  if (isDisplayReady()) {
    scheduleDisplayReinit(150UL);
  }

  DEBUG_PRINT("RELAY CMD: ");
  DEBUG_PRINT(on ? "ON" : "OFF");
  DEBUG_PRINT(" | mode=");
  DEBUG_PRINT(config.relayActiveHigh ? "ACTIVE_HIGH" : "ACTIVE_LOW");
  DEBUG_PRINT(" (Pin Level: ");
  DEBUG_PRINT(level == HIGH ? "HIGH" : "LOW");
  DEBUG_PRINTLN(")");
}

bool isRelayOn() { return digitalRead(RELAY_PIN) == relayOnLevel(); }

unsigned long IRAM_ATTR getRelayLastChangeUs() { return relayLastChangeUs; }
