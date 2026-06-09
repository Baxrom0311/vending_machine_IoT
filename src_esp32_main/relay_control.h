#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdbool.h>

enum RelayId {
  RELAY_1 = 0,
  RELAY_2 = 1,
};

// Relay control helpers (respect active-high/active-low config)
void setRelay(bool on);
void setRelay2(bool on);
bool isRelayOn();
bool isRelay2On();
void setDispenseOutputsActive(bool active);
void serviceRelayAutomation();
unsigned long getRelayLastChangeUs();

#endif
