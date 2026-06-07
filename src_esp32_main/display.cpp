#include "display.h"
#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "hardware.h"
#include "state_machine.h"
#include "uart_receiver.h"
#include <SPI.h>
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
static unsigned long displayReinitAtMs = 0;
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
static constexpr uint16_t TFT_BORDER_COLOR = ST77XX_WHITE;
static constexpr uint8_t TFT_BORDER_THICKNESS = 2;
static constexpr uint8_t TFT_CONTENT_PAD_X = 4;
static constexpr uint8_t TFT_FOOTER_TEXT_SIZE = 4;
static constexpr uint16_t TFT_FOOTER_COLOR = ST77XX_WHITE;
static constexpr uint16_t TFT_FOOTER_BOTTOM_MARGIN_PX = 8;
static constexpr char TFT_FOOTER_TEXT[] = "ECOCOMPANY";

static char lastFooterText[24] = {0};
static uint16_t lastFooterColor = 0;

// ============================================
// HELPERS
// ============================================
static uint16_t contentX() {
  return static_cast<uint16_t>(TFT_BORDER_THICKNESS + TFT_CONTENT_PAD_X);
}

static uint16_t contentWidthPx() {
  if (!displayReady) {
    return TFT_WIDTH;
  }
  const uint16_t sidePad =
      static_cast<uint16_t>((TFT_BORDER_THICKNESS + TFT_CONTENT_PAD_X) * 2U);
  if (lcd.width() <= sidePad) {
    return lcd.width();
  }
  return static_cast<uint16_t>(lcd.width() - sidePad);
}

static void drawDisplayBorder() {
  if (!displayReady) {
    return;
  }
  for (uint8_t i = 0; i < TFT_BORDER_THICKNESS; i++) {
    const uint16_t x = i;
    const uint16_t y = i;
    const uint16_t w = lcd.width() - static_cast<uint16_t>(2U * i);
    const uint16_t h = lcd.height() - static_cast<uint16_t>(2U * i);
    if (w == 0 || h == 0) {
      break;
    }
    lcd.drawRect(x, y, w, h, TFT_BORDER_COLOR);
  }
}

static uint8_t textCols() {
  if (!displayReady || TFT_TEXT_SIZE <= 0) {
    return 20;
  }

  const uint16_t charWidth = 6U * static_cast<uint16_t>(TFT_TEXT_SIZE);
  if (charWidth == 0) {
    return 20;
  }

  uint16_t cols = contentWidthPx() / charWidth;
  if (cols < 6) {
    cols = 6;
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
  return static_cast<uint16_t>(TFT_TOP_MARGIN_PX + TFT_BORDER_THICKNESS) +
         static_cast<uint16_t>(row) * lineBandHeightPx();
}

static uint16_t footerHeightPx() {
  return static_cast<uint16_t>(8U * static_cast<uint16_t>(TFT_FOOTER_TEXT_SIZE) +
                               4U);
}

static uint16_t footerY() {
  if (!displayReady) {
    return 0;
  }

  const uint16_t h = footerHeightPx();
  const uint16_t bottomSafe = static_cast<uint16_t>(TFT_BORDER_THICKNESS) +
                              static_cast<uint16_t>(TFT_FOOTER_BOTTOM_MARGIN_PX);
  if (lcd.height() <= h + bottomSafe) {
    return static_cast<uint16_t>(TFT_BORDER_THICKNESS);
  }
  return static_cast<uint16_t>(lcd.height() - h - bottomSafe);
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
  memset(lastFooterText, 0, sizeof(lastFooterText));
  lastFooterColor = 0;
}

static void renderFooterBrand() {
  if (!displayReady) {
    return;
  }

  const uint16_t y = footerY();
  const uint16_t h = footerHeightPx();
  const uint16_t textXMin = contentX();
  const uint16_t textAreaW = contentWidthPx();

  const uint16_t textWidth =
      static_cast<uint16_t>(strlen(TFT_FOOTER_TEXT) * 6U *
                            static_cast<uint16_t>(TFT_FOOTER_TEXT_SIZE));
  int32_t x = (static_cast<int32_t>(lcd.width()) -
               static_cast<int32_t>(textWidth)) /
              2;
  if (x < static_cast<int16_t>(textXMin)) {
    x = textXMin;
  }

  if (strcmp(lastFooterText, TFT_FOOTER_TEXT) == 0 &&
      lastFooterColor == TFT_FOOTER_COLOR) {
    return;
  }

  lcd.fillRect(textXMin, y, textAreaW, h, TFT_BG_COLOR);
  lcd.setTextSize(TFT_FOOTER_TEXT_SIZE);
  lcd.setTextColor(TFT_FOOTER_COLOR, TFT_BG_COLOR);
  lcd.setCursor(static_cast<int16_t>(x), static_cast<int16_t>(y + 2));
  lcd.print(TFT_FOOTER_TEXT);

  lcd.setTextSize(TFT_TEXT_SIZE);
  lcd.setTextColor(TFT_TEXT_COLOR, TFT_BG_COLOR);

  snprintf(lastFooterText, sizeof(lastFooterText), "%s", TFT_FOOTER_TEXT);
  lastFooterColor = TFT_FOOTER_COLOR;
}

static void forceFullDisplayRefresh(const char *reason) {
  if (!displayReady) {
    return;
  }

  lcd.fillScreen(TFT_BG_COLOR);
  drawDisplayBorder();
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
  drawDisplayBorder();
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

  const uint16_t textX = contentX();
  const uint16_t textW = contentWidthPx();
  lcd.fillRect(textX, y, textW, clearHeight, TFT_BG_COLOR);
  lcd.setTextColor(color, TFT_BG_COLOR);
  lcd.setCursor(static_cast<int16_t>(textX), static_cast<int16_t>(y + 2));
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
  renderFooterBrand();
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
  snprintf(out, outSize, "Narx: %d so'm/L", clampPricePerLiter());
}

static void formatBalanceLine(char *out, size_t outSize) {
  snprintf(out, outSize, "Balans:%ld", clampBalance());
}

static void formatRemainingWaterLine(char *out, size_t outSize) {
  const float remainingLiters = calculateAffordableLiters();
  if (currentState == DISPENSING || currentState == PAUSED) {
    snprintf(out, outSize, "Qolgan suv: %.2fL", remainingLiters);
  } else {
    snprintf(out, outSize, "Quyiladi: %.2fL", remainingLiters);
  }
}

static void formatLocalPaymentLine(char *out, size_t outSize) {
  snprintf(out, outSize, isPaymentEspConnected() ? "Naqd: ulangan"
                                                 : "Naqd: kutilyapti");
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
    formatLocalPaymentLine(line3, sizeof(line3));
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
  displayReinitAtMs = 0;
  lastDisplayRecoverAttemptMs = millis();
  lastFullRefreshMs = millis();
  hasRenderedState = false;
  lastRenderedState = currentState;

  renderMainScreen("Iltimos kuting...", "Local: tayyorlanmoqda");
}

// ============================================
// DISPLAY STATUS LINE CONTROL
// ============================================
void setDisplayNetworkStatus(const char *message) {
  const char *safeMessage = (message && message[0]) ? message : "N/A";
  snprintf(wifiStatusMessage, sizeof(wifiStatusMessage), "%s", safeMessage);
}

void scheduleDisplayReinit(unsigned long quietMs) {
  unsigned long now = millis();
  displayReinitAtMs = now + quietMs;
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

  if (displayReinitAtMs != 0 &&
      static_cast<long>(now - displayReinitAtMs) >= 0) {
    displayReinitAtMs = 0;
    if (initTftDriver()) {
      hasRenderedState = false;
      lastRenderedState = currentState;
      renderMainScreen("Display tiklandi", "Noise'dan keyin qayta init");
    } else {
      displayReady = false;
    }
  }

  if (!displayReady) {
    if (now - lastDisplayRecoverAttemptMs >= DISPLAY_RECOVER_RETRY_MS) {
      lastDisplayRecoverAttemptMs = now;
      if (initTftDriver()) {
        hasRenderedState = false;
        lastRenderedState = currentState;
        renderMainScreen("Display tiklandi", "Local: tekshirilmoqda");
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
