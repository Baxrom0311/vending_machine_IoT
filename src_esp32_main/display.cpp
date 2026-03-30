#include "display.h"
#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "state_machine.h"
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
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
static constexpr unsigned long DISPLAY_UPDATE_MIN_GAP_MS = 120UL;
static constexpr unsigned long DISPLAY_HEALTH_PROBE_MS = 4000UL;
static constexpr unsigned long DISPLAY_FOOTER_ROTATE_MS = 3000UL;
static constexpr unsigned long TEMP_MESSAGE_MIN_GAP_MS = 500UL;
static constexpr unsigned long TEMP_MESSAGE_DURATION_MS = 2400UL;
static constexpr unsigned long LCD_REINIT_SETTLE_MS = 150UL;
static constexpr uint8_t LCD_INIT_RETRY_COUNT = 3;

static LiquidCrystal_I2C lcdPrimary(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
static LiquidCrystal_I2C lcdFallback(LCD_I2C_ADDR_FALLBACK, LCD_COLS, LCD_ROWS);
static LiquidCrystal_I2C *activeLcd = &lcdPrimary;
static uint8_t activeLcdAddress = LCD_I2C_ADDR;

static bool displayReady = false;
static char lastLines[DISPLAY_LINE_COUNT][DISPLAY_LINE_BUFFER_SIZE] = {{0}};
static char wifiStatusMessage[DISPLAY_LINE_BUFFER_SIZE] = "WiFi tekshirilmoqda";
static char tempMessageLine1[DISPLAY_LINE_BUFFER_SIZE] = {0};
static char tempMessageLine2[DISPLAY_LINE_BUFFER_SIZE] = {0};
static unsigned long tempMessageEndTime = 0;
static unsigned long lastTempMessageSetMs = 0;
static unsigned long lastDisplayRecoverAttemptMs = 0;
static unsigned long lastDisplayUpdateMs = 0;
static unsigned long lastDisplayHealthProbeMs = 0;
static unsigned long displayReinitAtMs = 0;
static unsigned long footerRotationBaseMs = 0;
static SystemState lastRenderedState = IDLE;

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

static void resetFooterRotation() { footerRotationBaseMs = millis(); }

static void markDisplayUnavailable(const char *reason) {
  displayReady = false;
  clearRenderCache();
  resetFooterRotation();
  if (reason && reason[0] != '\0') {
    DEBUG_PRINT("LCD unavailable: ");
    DEBUG_PRINTLN(reason);
  }
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

static bool probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
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
  recoverI2cBus();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(LCD_I2C_CLOCK_HZ);

  for (uint8_t attempt = 0; attempt < LCD_INIT_RETRY_COUNT; attempt++) {
    if (!selectActiveLcd()) {
      delay(25);
      continue;
    }

    activeLcd->init();
    activeLcd->backlight();
    activeLcd->clear();
    clearRenderCache();
    displayReady = true;
    lastDisplayHealthProbeMs = millis();
    resetFooterRotation();

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
    return false;
  }

  return true;
}

static void writeLineCached(uint8_t row, const char *text) {
  if (!displayReady || !activeLcd || row >= DISPLAY_LINE_COUNT) {
    return;
  }

  char fixed[DISPLAY_LINE_BUFFER_SIZE];
  fitToLine(text, fixed);

  if (strcmp(lastLines[row], fixed) == 0) {
    return;
  }

  activeLcd->setCursor(0, row);
  activeLcd->print(fixed);
  copyBounded(lastLines[row], sizeof(lastLines[row]), fixed);
}

static void renderLines(const char *line0, const char *line1, const char *line2,
                        const char *line3) {
  writeLineCached(0, line0);
  writeLineCached(1, line1);
  writeLineCached(2, line2);
  writeLineCached(3, line3);
}

static const char *stateLabel(SystemState state) {
  switch (state) {
  case IDLE:
    return "KUTISH";
  case ACTIVE:
    return "TAYYOR";
  case DISPENSING:
    return "QUYISH";
  case PAUSED:
    return "PAUZA";
  default:
    return "NOMA'LUM";
  }
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

static void normalizeNetworkStatus(const char *raw, char *out, size_t outSize) {
  const bool wifiOk = (WiFi.status() == WL_CONNECTED);
  const bool mqttOk = mqttClient.connected();

  if (!isConfigured()) {
    snprintf(out, outSize, "Serial sozlang");
    return;
  }

  if (wifiOk && mqttOk) {
    snprintf(out, outSize, "WiFi+MQTT OK");
    return;
  }

  if (wifiOk) {
    if (raw && strcmp(raw, "Connected") == 0) {
      snprintf(out, outSize, "WiFi OK MQTT kut");
    } else {
      snprintf(out, outSize, "WiFi OK MQTT yoq");
    }
    return;
  }

  if (!raw || raw[0] == '\0') {
    snprintf(out, outSize, "WiFi ulanmayapti");
    return;
  }

  if (strcmp(raw, "Connecting...") == 0) {
    snprintf(out, outSize, "WiFi ulanmoqda");
    return;
  }
  if (strcmp(raw, "Disconnected") == 0) {
    snprintf(out, outSize, "WiFi uzildi");
    return;
  }
  if (strcmp(raw, "Failed") == 0) {
    snprintf(out, outSize, "WiFi xato");
    return;
  }
  if (strcmp(raw, "Not configured") == 0) {
    snprintf(out, outSize, "Serial sozlang");
    return;
  }
  if (strcmp(raw, "Connected") == 0) {
    snprintf(out, outSize, "WiFi OK");
    return;
  }

  copyBounded(out, outSize, raw);
}

static void buildLineState(char *out, size_t outSize) {
  snprintf(out, outSize, "Holat:%s", stateLabel(currentState));
}

static void buildLineBalance(char *out, size_t outSize) {
  char amount[12];
  formatMoneyValue(balance, amount, sizeof(amount));
  snprintf(out, outSize, "Balans:%s som", amount);
}

static void buildLineIdleHint(char *out, size_t outSize) {
  if (!isConfigured()) {
    snprintf(out, outSize, "Serial bilan sozlang");
    return;
  }

  const float affordable = calculateAffordableLiters();
  if (balance > 0) {
    char liters[10];
    formatLitersValue(affordable, liters, sizeof(liters));
    snprintf(out, outSize, "Start>%s", liters);
  } else {
    snprintf(out, outSize, "Pul kiriting");
  }
}

static void buildLineDispense(char *out, size_t outSize) {
  char dispensed[10];
  char remaining[10];
  formatLitersValue(totalDispensedLiters, dispensed, sizeof(dispensed));
  formatLitersValue(calculateAffordableLiters(), remaining, sizeof(remaining));
  snprintf(out, outSize, "Q:%s Qol:%s", dispensed, remaining);
}

static void buildLineAction(char *out, size_t outSize) {
  switch (currentState) {
  case IDLE:
    out[0] = '\0';
    break;
  case ACTIVE:
    snprintf(out, outSize, "START bosing");
    break;
  case DISPENSING:
    snprintf(out, outSize, "PAUSE bosib to'xta");
    break;
  case PAUSED:
    snprintf(out, outSize, "START bosib davom");
    break;
  default:
    snprintf(out, outSize, "Qayta ishga tushir");
    break;
  }
}

static void buildLineNetwork(char *out, size_t outSize) {
  normalizeNetworkStatus(wifiStatusMessage, out, outSize);
}

static void chooseFooterLine(const char *action, const char *network, char *out,
                             size_t outSize) {
  const bool hasAction = (action && action[0] != '\0');
  const bool hasNetwork = (network && network[0] != '\0');

  if (!hasAction && !hasNetwork) {
    out[0] = '\0';
    return;
  }
  if (!hasAction) {
    copyBounded(out, outSize, network);
    return;
  }
  if (!hasNetwork) {
    copyBounded(out, outSize, action);
    return;
  }

  const unsigned long now = millis();
  const bool showNetwork =
      ((now - footerRotationBaseMs) / DISPLAY_FOOTER_ROTATE_MS) % 2U != 0U;
  copyBounded(out, outSize, showNetwork ? network : action);
}

static void buildBootDetail(char *out, size_t outSize) {
  if (deviceConfig.device_id[0] != '\0') {
    snprintf(out, outSize, "ID:%s", deviceConfig.device_id);
    return;
  }
  snprintf(out, outSize, "LCD 20x4 tayyor");
}

static void renderStateScreen() {
  if (currentState != lastRenderedState) {
    lastRenderedState = currentState;
    resetFooterRotation();
  }

  char line0[DISPLAY_LINE_BUFFER_SIZE];
  char line1[DISPLAY_LINE_BUFFER_SIZE];
  char line2[DISPLAY_LINE_BUFFER_SIZE];
  char line3[DISPLAY_LINE_BUFFER_SIZE];
  char footerAction[DISPLAY_LINE_BUFFER_SIZE];
  char footerNetwork[DISPLAY_LINE_BUFFER_SIZE];

  buildLineState(line0, sizeof(line0));
  buildLineBalance(line1, sizeof(line1));

  if (currentState == DISPENSING || currentState == PAUSED) {
    buildLineDispense(line2, sizeof(line2));
  } else {
    buildLineIdleHint(line2, sizeof(line2));
  }

  buildLineAction(footerAction, sizeof(footerAction));
  buildLineNetwork(footerNetwork, sizeof(footerNetwork));
  chooseFooterLine(footerAction, footerNetwork, line3, sizeof(line3));
  renderLines(line0, line1, line2, line3);
}

static void renderTempScreen() {
  char line2[DISPLAY_LINE_BUFFER_SIZE];
  char line3[DISPLAY_LINE_BUFFER_SIZE];
  buildLineBalance(line2, sizeof(line2));
  buildLineNetwork(line3, sizeof(line3));
  renderLines(tempMessageLine1, tempMessageLine2[0] ? tempMessageLine2 : "",
              line2, line3);
}

void initDisplay() {
  copyBounded(wifiStatusMessage, sizeof(wifiStatusMessage),
              "WiFi tekshirilmoqda");
  tempMessageLine1[0] = '\0';
  tempMessageLine2[0] = '\0';
  tempMessageEndTime = 0;
  lastTempMessageSetMs = 0;
  lastDisplayRecoverAttemptMs = millis();
  lastDisplayUpdateMs = 0;
  lastDisplayHealthProbeMs = 0;
  displayReinitAtMs = 0;
  lastRenderedState = currentState;
  resetFooterRotation();

  if (!initLcdDriver()) {
    return;
  }

  char bootDetail[DISPLAY_LINE_BUFFER_SIZE];
  buildBootDetail(bootDetail, sizeof(bootDetail));
  renderLines("eWater boot", bootDetail, "Tizim ishga tushdi",
              "WiFi tekshirilmoqda");
}

void setDisplayNetworkStatus(const char *message) {
  const char *safeMessage = (message && message[0]) ? message : "";
  copyBounded(wifiStatusMessage, sizeof(wifiStatusMessage), safeMessage);
}

void scheduleDisplayReinit(unsigned long quietMs) {
  displayReinitAtMs =
      millis() + ((quietMs > 0) ? quietMs : LCD_REINIT_SETTLE_MS);
}

void showTemporaryMessage(const char *line1, const char *line2) {
  const unsigned long now = millis();
  const char *safeLine1 = line1 ? line1 : "";
  const char *safeLine2 = line2 ? line2 : "";
  const bool sameMessage =
      strcmp(tempMessageLine1, safeLine1) == 0 &&
      strcmp(tempMessageLine2, safeLine2) == 0;

  if (!sameMessage && (now - lastTempMessageSetMs) < TEMP_MESSAGE_MIN_GAP_MS) {
    return;
  }

  copyBounded(tempMessageLine1, sizeof(tempMessageLine1), safeLine1);
  copyBounded(tempMessageLine2, sizeof(tempMessageLine2), safeLine2);
  tempMessageEndTime = now + TEMP_MESSAGE_DURATION_MS;
  lastTempMessageSetMs = now;
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

  if (now < tempMessageEndTime && tempMessageLine1[0] != '\0') {
    renderTempScreen();
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
    renderLines("Holat xatosi", "Qayta yoqing", "", "");
    break;
  }
}

void displayIdle() { renderStateScreen(); }

void displayActive() { renderStateScreen(); }

void displayDispensing() { renderStateScreen(); }

void displayPaused() { renderStateScreen(); }

bool isDisplayReady() { return displayReady; }
