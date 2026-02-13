#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <Arduino.h>

// Firmware version is expected to be provided via PlatformIO build flags.
// (e.g. `-D FIRMWARE_VERSION=\"2.4.0-main\"`)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// OTA functions
void setupOTA();
void handleOTA();
void triggerOTAUpdate(const char *firmwareUrl);

#endif
