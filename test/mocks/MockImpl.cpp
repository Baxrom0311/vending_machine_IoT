#include "Arduino.h"

// Define global instances
__attribute__((weak)) SerialMock Serial;
// Preferences preferences; // Defined in config_storage.cpp

// Define millis mock
__attribute__((weak)) unsigned long _millis_mock = 0;

// Define ESP Mock
EspClass ESP;

// Define WiFi Mock
#include "WiFi.h"
WiFiClass WiFi;

// Define Display Mock
#include "display.h"
LiquidCrystal_I2C lcd;
TFT_eSPI tft;
void initDisplay() {}
void updateDisplay() {}
void displayIdle() {}
void displayActive() {}
void displayDispensing() {}
void displayFreeWater() {}
void displayPaused() {}
void showTemporaryMessage(const char *line1, const char *line2) {}
void displayStatus() {}
void displayError(const char *msg) {}

// Define Relay Mock
#include "../../src_esp32_main/relay_control.h"
void setRelay(bool on) {}
bool isRelayOn() { return false; }

// Define OTA Mock
#include "ota_handler.h"
void triggerOTAUpdate(const char *url) {}

// Define WDT Mock
#include "esp_task_wdt.h"
int wdt_reset_count = 0;
void esp_task_wdt_reset() { wdt_reset_count++; }

// Define Sensors Mock (readTDS)
// We need to declare it if we don't include sensors.h
int readTDS() { return 100; }

// Define Alerts Mock (legacy; kept for linkage even if unused)
enum AlertCategory { CAT_SYSTEM = 0 };
void alertCritical(AlertCategory cat, const char *msg) {
  std::cout << "ALERT_CRITICAL: " << msg << std::endl;
}
void alertWarning(AlertCategory cat, const char *msg) {}
void alertInfo(AlertCategory cat, const char *msg) {}
