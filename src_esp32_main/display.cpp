#include "display.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include "state_machine.h"
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef LCD_I2C_ADDR_FALLBACK
#define LCD_I2C_ADDR_FALLBACK 0x3F
#endif

#ifndef LCD_I2C_CLOCK_HZ
#define LCD_I2C_CLOCK_HZ 100000UL
#endif

static constexpr uint8_t DISPLAY_LINE_COUNT = LCD_ROWS;
static constexpr size_t DISPLAY_LINE_BUFFER_SIZE = LCD_COLS + 1;
static constexpr unsigned long DISPLAY_RECOVER_RETRY_MS = 1200UL;
static constexpr unsigned long DISPLAY_UPDATE_MIN_GAP_MS = 100UL;
static constexpr unsigned long DISPLAY_HEALTH_PROBE_MS = 4000UL;
static constexpr unsigned long DISPLAY_MAINTENANCE_IDLE_MS = 15000UL;
static constexpr unsigned long DISPLAY_MAINTENANCE_ACTIVE_MS = 5000UL;
static constexpr unsigned long DISPLAY_TEMP_MESSAGE_MS = 2400UL;
static constexpr unsigned long LCD_REINIT_SETTLE_MS = 150UL;
static constexpr uint8_t LCD_INIT_RETRY_COUNT = 3;
static constexpr uint8_t I2C_PROBE_RETRY_COUNT = 3;
static constexpr uint8_t LCD_FULL_REWRITE_PASSES = 2;
static constexpr uint16_t I2C_TRANSACTION_TIMEOUT_MS = 40U;
static constexpr unsigned long I2C_RETRY_GAP_MS = 2UL;

static LiquidCrystal_I2C lcdPrimary(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
static LiquidCrystal_I2C lcdFallback(LCD_I2C_ADDR_FALLBACK, LCD_COLS, LCD_ROWS);
static LiquidCrystal_I2C *activeLcd = &lcdPrimary;
static uint8_t activeLcdAddress = LCD_I2C_ADDR;

static bool displayReady = false;
static char lastLines[DISPLAY_LINE_COUNT][DISPLAY_LINE_BUFFER_SIZE] = {{0}};
static unsigned long lastDisplayRecoverAttemptMs = 0;
static unsigned long lastDisplayUpdateMs = 0;
static unsigned long lastDisplayHealthProbeMs = 0;
static unsigned long displayReinitAtMs = 0;
static unsigned long lastMaintenanceRewriteMs = 0;
static bool displayNeedsRewrite = true;
static char tempMessageLine0[DISPLAY_LINE_BUFFER_SIZE] = {0};
static char tempMessageLine1[DISPLAY_LINE_BUFFER_SIZE] = {0};
static char networkStatusLine[DISPLAY_LINE_BUFFER_SIZE] = {0};
static unsigned long tempMessageUntilMs = 0;
static bool tempMessageSticky = false;

static void clearRenderCache() {
  for (uint8_t row = 0; row < DISPLAY_LINE_COUNT; row++) {
    memset(lastLines[row], 0, sizeof(lastLines[row]));
  }
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

static void clearTemporaryMessageState() {
  tempMessageLine0[0] = '\0';
  tempMessageLine1[0] = '\0';
  tempMessageUntilMs = 0;
  tempMessageSticky = false;
}

static bool hasActiveTemporaryMessage(unsigned long now) {
  if (tempMessageLine0[0] == '\0' && tempMessageLine1[0] == '\0') {
    return false;
  }

  if (tempMessageSticky) {
    return true;
  }

  if (tempMessageUntilMs == 0) {
    return false;
  }

  if (static_cast<long>(now - tempMessageUntilMs) < 0) {
    return true;
  }

  clearTemporaryMessageState();
  return false;
}

static void fitToLine(const char *src, char *dst) {
  memset(dst, ' ', LCD_COLS);
  dst[LCD_COLS] = '\0';
  if (!src) {
    return;
  }

  size_t n = strlen(src);
  if (n > LCD_COLS) {
    n = LCD_COLS;
  }
  memcpy(dst, src, n);
}

static void recoverI2cBus();

static void markDisplayUnavailable(const char *reason) {
  displayReady = false;
  displayNeedsRewrite = true;
  lastDisplayRecoverAttemptMs = millis();
  clearRenderCache();
  if (reason && reason[0] != '\0') {
    DEBUG_PRINT("LCD unavailable: ");
    DEBUG_PRINTLN(reason);
  }
}

static bool beginWireBus() {
#if defined(WIRE_HAS_END)
  Wire.end();
  delay(I2C_RETRY_GAP_MS);
#endif

  recoverI2cBus();
  if (!Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    DEBUG_PRINTLN("LCD Wire.begin() failed");
    return false;
  }

  Wire.setClock(LCD_I2C_CLOCK_HZ);
  Wire.setTimeOut(I2C_TRANSACTION_TIMEOUT_MS);
  delay(I2C_RETRY_GAP_MS);
  return true;
}

static void recoverI2cBus() {
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  delayMicroseconds(10);

  if (digitalRead(I2C_SDA_PIN) == HIGH && digitalRead(I2C_SCL_PIN) == HIGH) {
    return;
  }

  pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SCL_PIN, HIGH);

  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(6);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(6);
  }

  pinMode(I2C_SDA_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA_PIN, LOW);
  delayMicroseconds(6);
  digitalWrite(I2C_SCL_PIN, HIGH);
  delayMicroseconds(6);
  digitalWrite(I2C_SDA_PIN, HIGH);
  delayMicroseconds(6);

  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
}

static bool probeI2cAddressOnce(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

static bool probeI2cAddress(uint8_t address) {
  for (uint8_t attempt = 0; attempt < I2C_PROBE_RETRY_COUNT; attempt++) {
    if (probeI2cAddressOnce(address)) {
      return true;
    }

    recoverI2cBus();
    delay(I2C_RETRY_GAP_MS);
  }

  return false;
}

static bool selectActiveLcd() {
  if (probeI2cAddress(LCD_I2C_ADDR)) {
    activeLcd = &lcdPrimary;
    activeLcdAddress = LCD_I2C_ADDR;
    return true;
  }

  if (LCD_I2C_ADDR_FALLBACK != LCD_I2C_ADDR &&
      probeI2cAddress(LCD_I2C_ADDR_FALLBACK)) {
    activeLcd = &lcdFallback;
    activeLcdAddress = LCD_I2C_ADDR_FALLBACK;
    return true;
  }

  return false;
}

static bool initLcdDriver() {
  if (!beginWireBus()) {
    markDisplayUnavailable("Wire begin failed");
    return false;
  }

  for (uint8_t attempt = 0; attempt < LCD_INIT_RETRY_COUNT; attempt++) {
    if (!selectActiveLcd()) {
      delay(25);
      continue;
    }

    activeLcd->init();
    delay(I2C_RETRY_GAP_MS);
    activeLcd->backlight();
    activeLcd->clear();
    delay(I2C_RETRY_GAP_MS);
    clearRenderCache();
    displayReady = true;
    displayNeedsRewrite = true;
    lastDisplayHealthProbeMs = millis();
    lastMaintenanceRewriteMs = 0;

    DEBUG_PRINT("LCD initialized at I2C 0x");
    DEBUG_PRINTLN(activeLcdAddress, HEX);
    return true;
  }

  markDisplayUnavailable("init failed after retries");
  return false;
}

static bool probeActiveLcdHealth(unsigned long now) {
  if (!displayReady) {
    return false;
  }

  if ((now - lastDisplayHealthProbeMs) < DISPLAY_HEALTH_PROBE_MS) {
    return true;
  }

  lastDisplayHealthProbeMs = now;
  if (!probeI2cAddress(activeLcdAddress)) {
    markDisplayUnavailable("I2C probe lost");
    displayReinitAtMs = millis() + LCD_REINIT_SETTLE_MS;
    return false;
  }

  return true;
}

static void requestDisplayRewrite() {
  displayNeedsRewrite = true;
  clearRenderCache();
}

static unsigned long displayMaintenanceIntervalMs() {
  switch (currentState) {
  case ACTIVE:
  case DISPENSING:
  case PAUSED:
    return DISPLAY_MAINTENANCE_ACTIVE_MS;
  case IDLE:
  default:
    return DISPLAY_MAINTENANCE_IDLE_MS;
  }
}

static bool scrubDisplaySurface(const char *reason) {
  if (!displayReady || !activeLcd) {
    return false;
  }

  if (reason && reason[0] != '\0') {
    DEBUG_PRINT("LCD scrub: ");
    DEBUG_PRINTLN(reason);
  }

  activeLcd->backlight();
  activeLcd->clear();
  delay(I2C_RETRY_GAP_MS);

  if (!probeI2cAddress(activeLcdAddress)) {
    markDisplayUnavailable("scrub failed");
    displayReinitAtMs = millis() + LCD_REINIT_SETTLE_MS;
    return false;
  }

  requestDisplayRewrite();
  return true;
}

static void writeLineCached(uint8_t row, const char *text) {
  if (!displayReady || !activeLcd || row >= DISPLAY_LINE_COUNT) {
    return;
  }

  char fixed[DISPLAY_LINE_BUFFER_SIZE];
  fitToLine(text, fixed);

  if (!displayNeedsRewrite && strcmp(lastLines[row], fixed) == 0) {
    return;
  }

  activeLcd->setCursor(0, row);
  activeLcd->print(fixed);
  copyBounded(lastLines[row], sizeof(lastLines[row]), fixed);
}

static void writeLineForced(uint8_t row, const char *text) {
  if (!displayReady || !activeLcd || row >= DISPLAY_LINE_COUNT) {
    return;
  }

  char fixed[DISPLAY_LINE_BUFFER_SIZE];
  fitToLine(text, fixed);

  activeLcd->setCursor(0, row);
  activeLcd->print(fixed);
  copyBounded(lastLines[row], sizeof(lastLines[row]), fixed);
}

static void renderLines(const char *line0, const char *line1, const char *line2,
                        const char *line3) {
  if (displayNeedsRewrite) {
    activeLcd->backlight();
    for (uint8_t pass = 0; pass < LCD_FULL_REWRITE_PASSES; pass++) {
      writeLineForced(0, line0);
      writeLineForced(1, line1);
      writeLineForced(2, line2);
      writeLineForced(3, line3);
      delay(I2C_RETRY_GAP_MS);
    }
    return;
  }

  writeLineCached(0, line0);
  writeLineCached(1, line1);
  writeLineCached(2, line2);
  writeLineCached(3, line3);
}

static float clampLiters(float liters) {
  if (!std::isfinite(liters) || liters < 0.0f) {
    return 0.0f;
  }
  if (liters > 999.9f) {
    return 999.9f;
  }
  return liters;
}

static float calculateAffordableLiters() {
  if (config.pricePerLiter <= 0 || balance <= 0) {
    return 0.0f;
  }

  const float liters =
      static_cast<float>(balance) / static_cast<float>(config.pricePerLiter);
  return clampLiters(liters);
}

static void formatMoneyValue(long amount, char *out, size_t outSize) {
  long safeAmount = amount;
  if (safeAmount < 0) {
    safeAmount = 0;
  }

  if (safeAmount < 1000000L) {
    snprintf(out, outSize, "%ld", safeAmount);
    return;
  }

  const float asThousands = static_cast<float>(safeAmount) / 1000.0f;
  snprintf(out, outSize, "%.1fk", asThousands);
}

static void formatLitersValue(float liters, char *out, size_t outSize) {
  const float safeLiters = clampLiters(liters);
  if (safeLiters < 10.0f) {
    snprintf(out, outSize, "%.2fL", safeLiters);
  } else if (safeLiters < 100.0f) {
    snprintf(out, outSize, "%.1fL", safeLiters);
  } else {
    snprintf(out, outSize, "%.0fL", safeLiters);
  }
}

static void buildLinePrice(char *out, size_t outSize) {
  char price[12];
  formatMoneyValue(config.pricePerLiter, price, sizeof(price));
  snprintf(out, outSize, "Narx:%s som/L", price);
}

static void buildLineBalance(char *out, size_t outSize) {
  char amount[12];
  formatMoneyValue(balance, amount, sizeof(amount));
  snprintf(out, outSize, "Balans:%s som", amount);
}

static void buildLineWater(char *out, size_t outSize) {
  char liters[10];
  formatLitersValue(calculateAffordableLiters(), liters, sizeof(liters));
  if (currentState == DISPENSING || currentState == PAUSED) {
    snprintf(out, outSize, "Qolgan:%s", liters);
  } else {
    snprintf(out, outSize, "Quyiladi:%s", liters);
  }
}

static void renderMainScreen() {
  char line0[DISPLAY_LINE_BUFFER_SIZE];
  char line1[DISPLAY_LINE_BUFFER_SIZE];
  char line2[DISPLAY_LINE_BUFFER_SIZE];

  buildLinePrice(line0, sizeof(line0));
  buildLineBalance(line1, sizeof(line1));
  buildLineWater(line2, sizeof(line2));
  renderLines(line0, line1, line2, "ECOCOMPANY");
  if (displayNeedsRewrite) {
    displayNeedsRewrite = false;
    lastMaintenanceRewriteMs = millis();
  }
}

static void renderTemporaryScreen() {
  char line0[DISPLAY_LINE_BUFFER_SIZE];
  char line1[DISPLAY_LINE_BUFFER_SIZE];
  char line2[DISPLAY_LINE_BUFFER_SIZE];

  if (tempMessageLine0[0] != '\0') {
    copyBounded(line0, sizeof(line0), tempMessageLine0);
  } else {
    buildLinePrice(line0, sizeof(line0));
  }

  if (tempMessageLine1[0] != '\0') {
    copyBounded(line1, sizeof(line1), tempMessageLine1);
  } else if (networkStatusLine[0] != '\0') {
    copyBounded(line1, sizeof(line1), networkStatusLine);
  } else {
    buildLineBalance(line1, sizeof(line1));
  }

  buildLineWater(line2, sizeof(line2));
  renderLines(line0, line1, line2, "ECOCOMPANY");
  if (displayNeedsRewrite) {
    displayNeedsRewrite = false;
    lastMaintenanceRewriteMs = millis();
  }
}

void initDisplay() {
  lastDisplayRecoverAttemptMs = millis();
  lastDisplayUpdateMs = 0;
  lastDisplayHealthProbeMs = 0;
  displayReinitAtMs = 0;
  lastMaintenanceRewriteMs = 0;
  displayNeedsRewrite = true;
  clearTemporaryMessageState();
  networkStatusLine[0] = '\0';

  if (!initLcdDriver()) {
    return;
  }

  renderMainScreen();
}

void setDisplayNetworkStatus(const char *message) {
  copyBounded(networkStatusLine, sizeof(networkStatusLine), message);
  if (tempMessageLine0[0] != '\0' || tempMessageLine1[0] != '\0') {
    requestDisplayRewrite();
  }
}

void scheduleDisplayReinit(unsigned long quietMs) {
  requestDisplayRewrite();
  displayReinitAtMs =
      millis() + ((quietMs > 0) ? quietMs : LCD_REINIT_SETTLE_MS);
}

void showTemporaryMessage(const char *line1, const char *line2) {
  if ((!line1 || line1[0] == '\0') && (!line2 || line2[0] == '\0')) {
    clearTemporaryMessageState();
    requestDisplayRewrite();
    return;
  }

  copyBounded(tempMessageLine0, sizeof(tempMessageLine0), line1);
  copyBounded(tempMessageLine1, sizeof(tempMessageLine1), line2);
  tempMessageSticky =
      (line1 && strcmp(line1, "SAFE MODE") == 0) ? true : false;
  tempMessageUntilMs = tempMessageSticky ? 0 : (millis() + DISPLAY_TEMP_MESSAGE_MS);
  requestDisplayRewrite();
}

void updateDisplay() {
  const unsigned long now = millis();

  if ((now - lastDisplayUpdateMs) < DISPLAY_UPDATE_MIN_GAP_MS) {
    return;
  }
  lastDisplayUpdateMs = now;

  if (displayReinitAtMs != 0 &&
      static_cast<long>(now - displayReinitAtMs) >= 0) {
    displayReinitAtMs = 0;
    initLcdDriver();
  }

  if (!displayReady) {
    if ((now - lastDisplayRecoverAttemptMs) >= DISPLAY_RECOVER_RETRY_MS) {
      lastDisplayRecoverAttemptMs = now;
      initLcdDriver();
    }
    return;
  }

  if (!probeActiveLcdHealth(now)) {
    return;
  }

  if ((now - lastMaintenanceRewriteMs) >= displayMaintenanceIntervalMs()) {
    if (!scrubDisplaySurface("periodic refresh")) {
      return;
    }
  }

  if (hasActiveTemporaryMessage(now)) {
    renderTemporaryScreen();
    return;
  }

  renderMainScreen();
}

void displayIdle() { renderMainScreen(); }

void displayActive() { renderMainScreen(); }

void displayDispensing() { renderMainScreen(); }

void displayPaused() { renderMainScreen(); }

bool isDisplayReady() { return displayReady; }
