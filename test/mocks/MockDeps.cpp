#include "../../src_esp32_main/hardware.h"
#include <iostream>

// ============================================
// CONFIG MOCK
// ============================================
#include "../../src_esp32_main/config.h"
__attribute__((weak)) Config config;

char device_id[32];
char admin_password[32];

void initConfig() {}
void applyRuntimeConfig() {}
