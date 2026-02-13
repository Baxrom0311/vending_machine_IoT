#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdbool.h>

// Relay control helpers (respect active-high/active-low config)
void setRelay(bool on);
bool isRelayOn();

#endif
