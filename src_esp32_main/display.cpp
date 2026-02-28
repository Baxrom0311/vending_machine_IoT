#include "display.h"
#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "state_machine.h"
#include <WiFi.h>
#include <Wire.h>
#include <cstdio>
#include <cstring>
#include <new>

// ============================================
// LCD OBJECT (20x4 by default, address from build flags)
// ============================================
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
static bool displayReady = false;

// ============================================
// TEMPORARY MESSAGE + NETWORK STATUS
// ============================================
static char tempMessageLine1[LCD_COLS + 1] = {0};
static char tempMessageLine2[LCD_COLS + 1] = {0};
static unsigned long tempMessageEndTime = 0;
static unsigned long lastTempMessageSetMs = 0;
static char wifiStatusMessage[32] = "kutilyapti";

// ============================================
// RENDER CACHE
// ============================================
static char lastLines[LCD_ROWS][LCD_COLS + 1] = {{0}};
static uint8_t lcdAddressInUse = LCD_I2C_ADDR;
static unsigned long lastDisplayRecoverAttemptMs = 0;
static unsigned long lastDisplayHealthCheckMs = 0;
static unsigned long lastFullRefreshMs = 0;
static SystemState lastRenderedState = IDLE;
static bool hasRenderedState = false;

static constexpr unsigned long DISPLAY_RECOVER_RETRY_MS = 800UL;
static constexpr unsigned long DISPLAY_HEALTH_CHECK_MS = 3000UL;
static constexpr unsigned long DISPLAY_PERIODIC_FULL_REFRESH_MS =
    5UL * 60UL * 1000UL; // 5 minutes
static constexpr unsigned long DISPLAY_STATE_REFRESH_MIN_GAP_MS = 1200UL;
static constexpr unsigned long TEMP_MESSAGE_MIN_GAP_MS = 500UL;
static constexpr uint16_t I2C_TIMEOUT_MS = 25;

static bool probeI2cAddress(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

static uint8_t detectLcdAddress() {
  // Try configured/default addresses first.
  const uint8_t preferred[] = {LCD_I2C_ADDR, 0x27, 0x3F};
  for (uint8_t i = 0; i < sizeof(preferred); i++) {
    const uint8_t addr = preferred[i];
    if (probeI2cAddress(addr)) {
      return addr;
    }
  }

  // Fallback scan for common PCF8574(A) ranges used by LCD backpacks.
  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    if (probeI2cAddress(addr)) {
      return addr;
    }
  }
  for (uint8_t addr = 0x38; addr <= 0x3F; addr++) {
    if (probeI2cAddress(addr)) {
      return addr;
    }
  }

  return 0;
}

// ============================================
// HELPERS
// ============================================
static void toFixedWidth(const char *src, char *out, size_t outSize) {
  const size_t width = LCD_COLS;
  if (outSize < width + 1) {
    return;
  }

  memset(out, ' ', width);
  out[width] = '\0';

  if (!src) {
    return;
  }

  size_t n = strlen(src);
  if (n > width) {
    n = width;
  }
  memcpy(out, src, n);
}

static void copyBounded(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dstSize, "%s", src);
}

static void clearRenderCache() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    memset(lastLines[row], 0, sizeof(lastLines[row]));
  }
}

static void forceFullDisplayRefresh(const char *reason, bool softReinit) {
  if (!displayReady) {
    return;
  }
  if (softReinit) {
    // Re-send controller init sequence to recover from occasional LCD desync.
    lcd.init();
    lcd.backlight();
  }
  lcd.clear();
  clearRenderCache();
  lastFullRefreshMs = millis();
  if (reason && reason[0]) {
    DEBUG_PRINT("ℹ️ LCD full refresh: ");
    DEBUG_PRINTLN(reason);
  }
}

static void recoverI2cBusIfStuck() {
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  delayMicroseconds(5);

  // If lines are already released, no need to pulse the clock.
  if (digitalRead(I2C_SDA_PIN) == HIGH && digitalRead(I2C_SCL_PIN) == HIGH) {
    return;
  }

  DEBUG_PRINTLN("⚠️ I2C bus busy, clock-unwedge...");

#if defined(ARDUINO_ARCH_ESP32)
  pinMode(I2C_SDA_PIN, OUTPUT_OPEN_DRAIN);
  pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
#else
  pinMode(I2C_SDA_PIN, OUTPUT);
  pinMode(I2C_SCL_PIN, OUTPUT);
#endif

  // Release both lines before pulsing.
  digitalWrite(I2C_SDA_PIN, HIGH);
  digitalWrite(I2C_SCL_PIN, HIGH);
  delayMicroseconds(5);

  // Up to 9 clock cycles are typically enough; use 18 as an upper bound.
  for (uint8_t i = 0; i < 18 && digitalRead(I2C_SDA_PIN) == LOW; i++) {
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(6);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(6);
  }

  // Try to generate a STOP condition.
  digitalWrite(I2C_SDA_PIN, LOW);
  delayMicroseconds(6);
  digitalWrite(I2C_SCL_PIN, HIGH);
  delayMicroseconds(6);
  digitalWrite(I2C_SDA_PIN, HIGH);
  delayMicroseconds(6);

  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
}

static void configureI2CBus() {
  // Release lines first, then reinitialize I2C at a conservative speed.
  recoverI2cBusIfStuck();
  delay(1);

#if defined(ARDUINO_ARCH_ESP32)
  Wire.end();
  delay(1);
#endif

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
#if defined(ARDUINO_ARCH_ESP32)
  Wire.setTimeOut(I2C_TIMEOUT_MS);
#endif
}

static bool initLcdDriver(bool recoveryMode) {
  configureI2CBus();

  const uint8_t detectedAddr = detectLcdAddress();
  if (detectedAddr == 0) {
    if (recoveryMode) {
      DEBUG_PRINTLN("⚠️ LCD recovery failed: I2C ACK yo'q");
    } else {
      DEBUG_PRINTLN("⚠️ LCD not detected on I2C bus");
    }
    displayReady = false;
    return false;
  }

  lcdAddressInUse = detectedAddr;
  if (detectedAddr != LCD_I2C_ADDR) {
    DEBUG_PRINT("ℹ️ LCD address override: 0x");
    DEBUG_PRINT(LCD_I2C_ADDR, HEX);
    DEBUG_PRINT(" -> 0x");
    DEBUG_PRINTLN(detectedAddr, HEX);
  }

  new (&lcd) LiquidCrystal_I2C(detectedAddr, LCD_COLS, LCD_ROWS);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  displayReady = true;
  clearRenderCache();
  return true;
}

static void writeLineCached(uint8_t row, const char *text) {
  if (!displayReady || row >= LCD_ROWS) {
    return;
  }

  char fixed[LCD_COLS + 1];
  toFixedWidth(text, fixed, sizeof(fixed));

  if (strcmp(lastLines[row], fixed) == 0) {
    return;
  }

  lcd.setCursor(0, row);
  lcd.print(fixed);
  strcpy(lastLines[row], fixed);
}

static void renderScreen(const char *line0, const char *line1, const char *line2,
                         const char *line3) {
  writeLineCached(0, line0 ? line0 : "");
  if (LCD_ROWS > 1) {
    writeLineCached(1, line1 ? line1 : "");
  }
  if (LCD_ROWS > 2) {
    writeLineCached(2, line2 ? line2 : "");
  }
  if (LCD_ROWS > 3) {
    writeLineCached(3, line3 ? line3 : "");
  }
}

static float calculateAffordableLiters() {
  if (config.pricePerLiter <= 0 || balance <= 0) {
    return 0.0f;
  }

  float liters =
      static_cast<float>(balance) / static_cast<float>(config.pricePerLiter);
  if (liters < 0.0f) {
    liters = 0.0f;
  }
  if (liters > 999.99f) {
    liters = 999.99f;
  }
  return liters;
}

static void formatTopLine(char *out, size_t outSize) {
  int shownPrice = config.pricePerLiter;
  if (shownPrice < 0) {
    shownPrice = 0;
  }
  if (shownPrice > 9999999) {
    shownPrice = 9999999;
  }
  snprintf(out, outSize, "Narx: %d so'm/L", shownPrice);
}

static void formatBalanceCapacityLine(char *out, size_t outSize) {
  long shownBalance = balance;
  if (shownBalance < 0) {
    shownBalance = 0;
  }
  if (shownBalance > 9999999L) {
    shownBalance = 9999999L;
  }

  snprintf(out, outSize, "Balans: %ld", shownBalance);
}

static void formatRemainingWaterLine(char *out, size_t outSize) {
  const float remainingLiters = calculateAffordableLiters();
  if (currentState == DISPENSING || currentState == PAUSED) {
    snprintf(out, outSize, "Qolgan suv:%.2fL", remainingLiters);
  } else {
    snprintf(out, outSize, "Quyiladi:%.2fL", remainingLiters);
  }
}

static void formatOnlinePaymentLine(char *out, size_t outSize) {
  if (!isConfigured()) {
    snprintf(out, outSize, "Onlayn: sozlanmagan");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    const bool connecting =
        (strstr(wifiStatusMessage, "Connecting") != nullptr) ||
        (strstr(wifiStatusMessage, "connecting") != nullptr);
    snprintf(out, outSize, connecting ? "Onlayn: ulanmoqda"
                                      : "Onlayn: WiFi yo'q");
    return;
  }

  if (mqttClient.connected()) {
    snprintf(out, outSize, "Onlayn to'lov: OK");
  } else {
    snprintf(out, outSize, "Onlayn: server kut.");
  }
}

static void renderMainScreen(const char *line2Override,
                             const char *line3Override) {
  char line0[32];
  char line1[32];
  char line2[32];
  char line3[32];

  formatTopLine(line0, sizeof(line0));
  formatBalanceCapacityLine(line1, sizeof(line1));

  if (line2Override && line2Override[0]) {
    copyBounded(line2, sizeof(line2), line2Override);
  } else {
    formatRemainingWaterLine(line2, sizeof(line2));
  }

  if (line3Override && line3Override[0]) {
    copyBounded(line3, sizeof(line3), line3Override);
  } else {
    formatOnlinePaymentLine(line3, sizeof(line3));
  }

  renderScreen(line0, line1, line2, line3);
}

// ============================================
// INITIALIZATION
// ============================================
void initDisplay() {
  if (!initLcdDriver(false)) {
    return;
  }

  tempMessageLine1[0] = '\0';
  tempMessageLine2[0] = '\0';
  tempMessageEndTime = 0;
  lastTempMessageSetMs = 0;
  lastDisplayRecoverAttemptMs = millis();
  lastDisplayHealthCheckMs = millis();
  lastFullRefreshMs = millis();
  hasRenderedState = false;
  lastRenderedState = currentState;

  renderMainScreen("Iltimos kuting...", "Onlayn: tekshirilmoqda");
}

// ============================================
// DISPLAY STATUS LINE CONTROL
// ============================================
void setDisplayNetworkStatus(const char *message) {
  const char *safeMessage = (message && message[0]) ? message : "N/A";
  snprintf(wifiStatusMessage, sizeof(wifiStatusMessage), "%s", safeMessage);
}

// ============================================
// TEMPORARY MESSAGE
// ============================================
void showTemporaryMessage(const char *line1, const char *line2) {
  const unsigned long now = millis();
  if (now - lastTempMessageSetMs < TEMP_MESSAGE_MIN_GAP_MS) {
    return;
  }

  copyBounded(tempMessageLine1, sizeof(tempMessageLine1), line1 ? line1 : "");
  copyBounded(tempMessageLine2, sizeof(tempMessageLine2), line2 ? line2 : "");
  tempMessageEndTime = now + 2000;
  lastTempMessageSetMs = now;
}

// ============================================
// DISPLAY UPDATE
// ============================================
void updateDisplay() {
  const unsigned long now = millis();

  if (!displayReady) {
    if (now - lastDisplayRecoverAttemptMs >= DISPLAY_RECOVER_RETRY_MS) {
      lastDisplayRecoverAttemptMs = now;
      DEBUG_PRINTLN("⚠️ LCD offline, recover attempt...");
      if (initLcdDriver(true)) {
        DEBUG_PRINTLN("✓ LCD recovered");
        lastFullRefreshMs = millis();
        hasRenderedState = false;
        lastRenderedState = currentState;
        renderMainScreen("Ishlashda davom...", "Onlayn: tekshirilmoqda");
      }
    }
    return;
  }

  if (now - lastDisplayHealthCheckMs >= DISPLAY_HEALTH_CHECK_MS) {
    lastDisplayHealthCheckMs = now;
    if (!probeI2cAddress(lcdAddressInUse)) {
      DEBUG_PRINTLN("⚠️ LCD ACK yo'q, qayta ulanyapti...");
      displayReady = false;
    }
  }

  if (!displayReady) {
    return;
  }

  static unsigned long lastUpdateMs = 0;
  if (now - lastUpdateMs < 100) {
    return;
  }
  lastUpdateMs = now;

  const bool stateChanged =
      (!hasRenderedState) || (currentState != lastRenderedState);
  const bool periodicRefreshDue =
      (now - lastFullRefreshMs) >= DISPLAY_PERIODIC_FULL_REFRESH_MS;

  if (stateChanged) {
    // Avoid aggressive clear() storms when buttons are spammed.
    if ((now - lastFullRefreshMs) >= DISPLAY_STATE_REFRESH_MIN_GAP_MS) {
      forceFullDisplayRefresh("state change", false);
    } else {
      clearRenderCache(); // force redraw without expensive lcd.clear()
    }
  } else if (periodicRefreshDue) {
    forceFullDisplayRefresh("periodic scrub", true);
  }

  if (now < tempMessageEndTime && tempMessageLine1[0] != '\0') {
    renderMainScreen(tempMessageLine1,
                     tempMessageLine2[0] ? tempMessageLine2 : nullptr);
    return;
  }

  if (now >= tempMessageEndTime) {
    tempMessageLine1[0] = '\0';
    tempMessageLine2[0] = '\0';
  }

  switch (currentState) {
  case IDLE:
    displayIdle();
    break;
  case ACTIVE:
    displayActive();
    break;
  case DISPENSING:
    displayDispensing();
    break;
  case PAUSED:
    displayPaused();
    break;
  default:
    renderMainScreen("Holat xatosi", "Qayta yoqing");
    break;
  }

  lastRenderedState = currentState;
  hasRenderedState = true;
}

// ============================================
// STATE DISPLAYS (optimized for 20x4)
// ============================================
void displayIdle() {
  renderMainScreen(nullptr, nullptr);
}

void displayActive() { renderMainScreen(nullptr, nullptr); }

void displayDispensing() { renderMainScreen(nullptr, nullptr); }

void displayPaused() { renderMainScreen(nullptr, nullptr); }

bool isDisplayReady() { return displayReady; }
