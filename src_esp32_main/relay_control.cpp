#include "relay_control.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include <Arduino.h>

struct RelayRuntime {
  bool automationActive;
  bool outputOn;
  bool cyclePhaseOn;
  unsigned long phaseStartedMs;
};

static volatile unsigned long relayLastChangeUs = 0;
static RelayRuntime relayRuntime[2] = {
    {false, false, false, 0},
    {false, false, false, 0},
};

static uint8_t relayPinFor(RelayId relay) {
  return relay == RELAY_2 ? RELAY2_PIN : RELAY_PIN;
}

static int relayOnLevel() { return config.relayActiveHigh ? HIGH : LOW; }

static int relayOffLevel() { return config.relayActiveHigh ? LOW : HIGH; }

static unsigned long relayOnMsFor(RelayId relay) {
  return relay == RELAY_2 ? config.relay2OnMs : config.relay1OnMs;
}

static unsigned long relayOffMsFor(RelayId relay) {
  return relay == RELAY_2 ? config.relay2OffMs : config.relay1OffMs;
}

static RelayRuntime &runtimeFor(RelayId relay) {
  return relayRuntime[relay == RELAY_2 ? 1 : 0];
}

static void setRelayById(RelayId relay, bool on) {
  RelayRuntime &runtime = runtimeFor(relay);
  runtime.outputOn = on;

  const int level = on ? relayOnLevel() : relayOffLevel();
  digitalWrite(relayPinFor(relay), level);
  relayLastChangeUs = micros();

  DEBUG_PRINT("RELAY");
  DEBUG_PRINT(relay == RELAY_2 ? '2' : '1');
  DEBUG_PRINT(" CMD: ");
  DEBUG_PRINT(on ? "ON" : "OFF");
  DEBUG_PRINT(" | mode=");
  DEBUG_PRINT(config.relayActiveHigh ? "ACTIVE_HIGH" : "ACTIVE_LOW");
  DEBUG_PRINT(" (Pin Level: ");
  DEBUG_PRINT(level == HIGH ? "HIGH" : "LOW");
  DEBUG_PRINTLN(")");
}

static bool isRelayOnById(RelayId relay) {
  return digitalRead(relayPinFor(relay)) == relayOnLevel();
}

static void startRelayCycle(RelayId relay, bool active) {
  RelayRuntime &runtime = runtimeFor(relay);
  runtime.automationActive = active;
  runtime.phaseStartedMs = millis();
  runtime.cyclePhaseOn = active;
  setRelayById(relay, active);
}

void setRelay(bool on) {
  runtimeFor(RELAY_1).automationActive = false;
  setRelayById(RELAY_1, on);
}

void setRelay2(bool on) {
  runtimeFor(RELAY_2).automationActive = false;
  setRelayById(RELAY_2, on);
}

bool isRelayOn() { return isRelayOnById(RELAY_1); }

bool isRelay2On() { return isRelayOnById(RELAY_2); }

void setDispenseOutputsActive(bool active) {
  startRelayCycle(RELAY_1, active);
  startRelayCycle(RELAY_2, active);
}

void serviceRelayAutomation() {
  const unsigned long now = millis();

  for (RelayId relay : {RELAY_1, RELAY_2}) {
    RelayRuntime &runtime = runtimeFor(relay);
    const unsigned long offMs = relayOffMsFor(relay);

    if (!runtime.automationActive) {
      continue;
    }

    if (offMs == 0) {
      continue;
    }

    const unsigned long onMs =
        relayOnMsFor(relay) == 0 ? 1000UL : relayOnMsFor(relay);
    const unsigned long phaseDuration =
        runtime.cyclePhaseOn ? onMs : offMs;

    if ((now - runtime.phaseStartedMs) < phaseDuration) {
      continue;
    }

    runtime.phaseStartedMs = now;
    runtime.cyclePhaseOn = !runtime.cyclePhaseOn;
    setRelayById(relay, runtime.cyclePhaseOn);
  }
}

unsigned long IRAM_ATTR getRelayLastChangeUs() { return relayLastChangeUs; }
