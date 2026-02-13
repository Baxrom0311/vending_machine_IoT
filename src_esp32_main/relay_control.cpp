#include "relay_control.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include <Arduino.h>

// Hardware policy: relay module is wired Active-HIGH in this project.
// ON  -> GPIO HIGH
// OFF -> GPIO LOW
static int relayOnLevel() { return HIGH; }

static int relayOffLevel() { return LOW; }

void setRelay(bool on) {
  config.relayActiveHigh = true; // keep runtime config aligned with HW policy
  int level = on ? relayOnLevel() : relayOffLevel();
  digitalWrite(RELAY_PIN, level);

  DEBUG_PRINT("RELAY CMD: ");
  DEBUG_PRINT(on ? "ON" : "OFF");
  DEBUG_PRINT(" | mode=");
  DEBUG_PRINT("ACTIVE_HIGH");
  DEBUG_PRINT(" (Pin Level: ");
  DEBUG_PRINT(level == HIGH ? "HIGH" : "LOW");
  DEBUG_PRINTLN(")");
}

bool isRelayOn() { return digitalRead(RELAY_PIN) == relayOnLevel(); }
