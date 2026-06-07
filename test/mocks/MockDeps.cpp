#include "../../src_esp32_main/hardware.h"
#include <iostream>

// ============================================
// CONFIG MOCK
// ============================================
#include "../../src_esp32_main/config.h"
__attribute__((weak)) Config config;

char wifi_ssid[32];
char wifi_password[64];
char device_id[32];
char admin_password[32];
int tdsPPM = 0;

void setupWiFi() {}
void processWiFi() {}
void initConfig() {}
void applyRuntimeConfig() {}
