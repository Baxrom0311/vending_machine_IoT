#include "display.h"
#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "state_machine.h"
#include <SPI.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

// ============================================
// TFT OBJECT
// ============================================
Adafruit_ST7789 lcd(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static bool displayReady = false;

// ============================================
// DISPLAY STATE
// ============================================
static constexpr uint8_t DISPLAY_LINE_COUNT = 4;
static constexpr size_t DISPLAY_LINE_BUFFER_SIZE = 64;

static char tempMessageLine1[DISPLAY_LINE_BUFFER_SIZE] = {0};
static char tempMessageLine2[DISPLAY_LINE_BUFFER_SIZE] = {0};
static unsigned long tempMessageEndTime = 0;
static unsigned long lastTempMessageSetMs = 0;
static char wifiStatusMessage[32] = "kutilyapti";

static char lastLines[DISPLAY_LINE_COUNT][DISPLAY_LINE_BUFFER_SIZE] = {{0}};
static uint16_t lastLineColors[DISPLAY_LINE_COUNT] = {0};

static unsigned long lastDisplayRecoverAttemptMs = 0;
static unsigned long lastFullRefreshMs = 0;
static SystemState lastRenderedState = IDLE;
static bool hasRenderedState = false;

static constexpr unsigned long DISPLAY_RECOVER_RETRY_MS = 1000UL;
static constexpr unsigned long DISPLAY_PERIODIC_FULL_REFRESH_MS =
    5UL * 60UL * 1000UL;
static constexpr unsigned long DISPLAY_STATE_REFRESH_MIN_GAP_MS = 1200UL;
static constexpr unsigned long TEMP_MESSAGE_MIN_GAP_MS = 500UL;
static constexpr unsigned long DISPLAY_UPDATE_MIN_GAP_MS = 100UL;

static constexpr uint16_t TFT_BG_COLOR = ST77XX_BLACK;
static constexpr uint16_t TFT_TEXT_COLOR = ST77XX_WHITE;
static constexpr uint16_t TFT_ACCENT_COLOR = ST77XX_CYAN;
static constexpr uint16_t TFT_OK_COLOR = ST77XX_GREEN;
static constexpr uint16_t TFT_WARN_COLOR = ST77XX_YELLOW;

// ============================================
// HELPERS
// ============================================
static uint8_t textCols() {
  if (!displayReady) {
    return 20;
  }

  const uint16_t charWidth = 6U * static_cast<uint16_t>(TFT_TEXT_SIZE);
  if (charWidth == 0) {
    return 20;
  }

  uint16_t cols = lcd.width() / charWidth;
  if (cols < 12) {
    cols = 12;
  }

  const uint16_t maxCols = static_cast<uint16_t>(DISPLAY_LINE_BUFFER_SIZE - 1);
  if (cols > maxCols) {
    cols = maxCols;
  }

  return static_cast<uint8_t>(cols);
}

static uint16_t lineHeightPx() {
  return static_cast<uint16_t>(8U * static_cast<uint16_t>(TFT_TEXT_SIZE) + 4U);
}

static uint16_t lineBandHeightPx() {
  return static_cast<uint16_t>(lineHeightPx() +
                               static_cast<uint16_t>(TFT_LINE_SPACING_PX));
}

static uint16_t lineY(uint8_t row) {
  return static_cast<uint16_t>(TFT_TOP_MARGIN_PX) +
         static_cast<uint16_t>(row) * lineBandHeightPx();
}

static void toFixedWidth(const char *src, char *out, size_t outSize) {
  const size_t width = textCols();
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
  for (uint8_t row = 0; row < DISPLAY_LINE_COUNT; row++) {
    memset(lastLines[row], 0, sizeof(lastLines[row]));
    lastLineColors[row] = 0;
  }
}

static void forceFullDisplayRefresh(const char *reason) {
  if (!displayReady) {
    return;
  }

  lcd.fillScreen(TFT_BG_COLOR);
  clearRenderCache();
  lastFullRefreshMs = millis();

  if (reason && reason[0]) {
    DEBUG_PRINT("Display refresh: ");
    DEBUG_PRINTLN(reason);
  }
}

static bool initTftDriver() {
  SPI.begin(TFT_SCLK_PIN, TFT_MISO_PIN, TFT_MOSI_PIN, TFT_CS_PIN);

  lcd.init(TFT_WIDTH, TFT_HEIGHT);
  lcd.setRotation(TFT_ROTATION);
  lcd.setTextWrap(false);
  lcd.setTextSize(TFT_TEXT_SIZE);
  lcd.setTextColor(TFT_TEXT_COLOR, TFT_BG_COLOR);
  lcd.fillScreen(TFT_BG_COLOR);

  if (lcd.width() == 0 || lcd.height() == 0) {
    displayReady = false;
    return false;
  }

  displayReady = true;
  clearRenderCache();
  lastFullRefreshMs = millis();
  return true;
}

static void writeLineCached(uint8_t row, const char *text, uint16_t color) {
  if (!displayReady || row >= DISPLAY_LINE_COUNT) {
    return;
  }

  char fixed[DISPLAY_LINE_BUFFER_SIZE];
  toFixedWidth(text, fixed, sizeof(fixed));

  if (strcmp(lastLines[row], fixed) == 0 && lastLineColors[row] == color) {
    return;
  }

  const uint16_t y = lineY(row);
  if (y >= lcd.height()) {
    return;
  }

  const uint16_t bandHeight = lineBandHeightPx();
  const uint16_t remaining = static_cast<uint16_t>(lcd.height() - y);
  const uint16_t clearHeight = (bandHeight < remaining) ? bandHeight : remaining;

  lcd.fillRect(0, y, lcd.width(), clearHeight, TFT_BG_COLOR);
  lcd.setTextColor(color, TFT_BG_COLOR);
  lcd.setCursor(0, static_cast<int16_t>(y + 2));
  lcd.print(fixed);
  lcd.setTextColor(TFT_TEXT_COLOR, TFT_BG_COLOR);

  snprintf(lastLines[row], sizeof(lastLines[row]), "%s", fixed);
  lastLineColors[row] = color;
}

static uint16_t networkLineColor(const char *line) {
  if (!line) {
    return TFT_TEXT_COLOR;
  }
  if (strstr(line, "OK") != nullptr) {
    return TFT_OK_COLOR;
  }
  return TFT_WARN_COLOR;
}

static void renderScreen(const char *line0, const char *line1, const char *line2,
                         const char *line3) {
  writeLineCached(0, line0 ? line0 : "", TFT_ACCENT_COLOR);
  writeLineCached(1, line1 ? line1 : "", TFT_TEXT_COLOR);
  writeLineCached(2, line2 ? line2 : "", TFT_TEXT_COLOR);
  writeLineCached(3, line3 ? line3 : "", networkLineColor(line3));
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

static int clampPricePerLiter() {
  int shownPrice = config.pricePerLiter;
  if (shownPrice < 0) {
    shownPrice = 0;
  }
  if (shownPrice > 9999999) {
    shownPrice = 9999999;
  }
  return shownPrice;
}

static long clampBalance() {
  long shownBalance = balance;
  if (shownBalance < 0) {
    shownBalance = 0;
  }
  if (shownBalance > 9999999L) {
    shownBalance = 9999999L;
  }
  return shownBalance;
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

static void formatTopLine(char *out, size_t outSize) {
  snprintf(out, outSize, "%s | %d so'm/L", stateLabel(currentState),
           clampPricePerLiter());
}

static void formatBalanceLine(char *out, size_t outSize) {
  snprintf(out, outSize, "Balans: %ld", clampBalance());
}

static void formatRemainingWaterLine(char *out, size_t outSize) {
  const float remainingLiters = calculateAffordableLiters();
  if (currentState == DISPENSING || currentState == PAUSED) {
    snprintf(out, outSize, "Qolgan suv: %.2fL", remainingLiters);
  } else {
    snprintf(out, outSize, "Quyiladi: %.2fL", remainingLiters);
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
  char line0[DISPLAY_LINE_BUFFER_SIZE];
  char line1[DISPLAY_LINE_BUFFER_SIZE];
  char line2[DISPLAY_LINE_BUFFER_SIZE];
  char line3[DISPLAY_LINE_BUFFER_SIZE];

  formatTopLine(line0, sizeof(line0));
  formatBalanceLine(line1, sizeof(line1));

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
  if (!initTftDriver()) {
    return;
  }

  tempMessageLine1[0] = '\0';
  tempMessageLine2[0] = '\0';
  tempMessageEndTime = 0;
  lastTempMessageSetMs = 0;
  lastDisplayRecoverAttemptMs = millis();
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
      if (initTftDriver()) {
        hasRenderedState = false;
        lastRenderedState = currentState;
        renderMainScreen("Display tiklandi", "Onlayn: tekshirilmoqda");
      }
    }
    return;
  }

  static unsigned long lastUpdateMs = 0;
  if (now - lastUpdateMs < DISPLAY_UPDATE_MIN_GAP_MS) {
    return;
  }
  lastUpdateMs = now;

  const bool stateChanged =
      (!hasRenderedState) || (currentState != lastRenderedState);
  const bool periodicRefreshDue =
      (now - lastFullRefreshMs) >= DISPLAY_PERIODIC_FULL_REFRESH_MS;

  if (stateChanged) {
    if ((now - lastFullRefreshMs) >= DISPLAY_STATE_REFRESH_MIN_GAP_MS) {
      forceFullDisplayRefresh("state change");
    } else {
      clearRenderCache();
    }
  } else if (periodicRefreshDue) {
    forceFullDisplayRefresh("periodic scrub");
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
// STATE DISPLAYS
// ============================================
void displayIdle() { renderMainScreen(nullptr, nullptr); }

void displayActive() { renderMainScreen(nullptr, nullptr); }

void displayDispensing() { renderMainScreen(nullptr, nullptr); }

void displayPaused() { renderMainScreen(nullptr, nullptr); }

bool isDisplayReady() { return displayReady; }
