#include "display.h"
#include "config.h"
#include "hardware.h"
#include "state_machine.h"
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
static char networkStatusLine[32] = "WiFi: kutilyapti";

// ============================================
// RENDER CACHE
// ============================================
static char lastLines[LCD_ROWS][LCD_COLS + 1] = {{0}};

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

static void formatBalanceLine(char *out, size_t outSize) {
  if (balance > 999999999L) {
    snprintf(out, outSize, "Balans: 999999999");
    return;
  }
  if (balance < -99999999L) {
    snprintf(out, outSize, "Balans: -99999999");
    return;
  }
  snprintf(out, outSize, "Balans: %ld", balance);
}

static void formatDispensedLine(char *out, size_t outSize, float liters) {
  if (liters < 0.0f) {
    liters = 0.0f;
  }
  if (liters > 9999.99f) {
    liters = 9999.99f;
  }
  snprintf(out, outSize, "Quyildi: %.2f L", liters);
}

static void formatFreeWaterLine(char *out, size_t outSize) {
  float dispensed = freeWaterDispensed;
  float limit = config.freeWaterAmount;

  if (dispensed < 0.0f) {
    dispensed = 0.0f;
  }
  if (limit < 0.0f) {
    limit = 0.0f;
  }
  if (dispensed > 999.99f) {
    dispensed = 999.99f;
  }
  if (limit > 999.99f) {
    limit = 999.99f;
  }

  snprintf(out, outSize, "Bepul: %.2f/%.2f L", dispensed, limit);
}

static void renderStateScreen(const char *title, const char *detail,
                              const char *footerOverride) {
  char balanceLine[32];
  formatBalanceLine(balanceLine, sizeof(balanceLine));
  const char *footer = (footerOverride && footerOverride[0])
                           ? footerOverride
                           : networkStatusLine;
  renderScreen(title, balanceLine, detail, footer);
}

// ============================================
// INITIALIZATION
// ============================================
void initDisplay() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  const uint8_t detectedAddr = detectLcdAddress();
  if (detectedAddr == 0) {
    Serial.println("⚠️ LCD not detected on I2C bus");
    displayReady = false;
    return;
  }

  if (detectedAddr != LCD_I2C_ADDR) {
    Serial.print("ℹ️ LCD address override: 0x");
    Serial.print(LCD_I2C_ADDR, HEX);
    Serial.print(" -> 0x");
    Serial.println(detectedAddr, HEX);
    new (&lcd) LiquidCrystal_I2C(detectedAddr, LCD_COLS, LCD_ROWS);
  }

  lcd.init();
  lcd.backlight();
  lcd.clear();
  displayReady = true;

  tempMessageLine1[0] = '\0';
  tempMessageLine2[0] = '\0';
  tempMessageEndTime = 0;
  clearRenderCache();

  renderScreen("TIZIM YUKLANMOQDA", "Iltimos kuting...", "Balans: 0",
               networkStatusLine);
}

// ============================================
// DISPLAY STATUS LINE CONTROL
// ============================================
void setDisplayNetworkStatus(const char *message) {
  const char *safeMessage = (message && message[0]) ? message : "N/A";
  snprintf(networkStatusLine, sizeof(networkStatusLine), "WiFi: %s",
           safeMessage);

  if (!displayReady) {
    return;
  }

  const uint8_t statusRow = (LCD_ROWS > 0) ? (LCD_ROWS - 1) : 0;
  writeLineCached(statusRow, networkStatusLine);
}

// ============================================
// TEMPORARY MESSAGE
// ============================================
void showTemporaryMessage(const char *line1, const char *line2) {
  copyBounded(tempMessageLine1, sizeof(tempMessageLine1), line1 ? line1 : "");
  copyBounded(tempMessageLine2, sizeof(tempMessageLine2), line2 ? line2 : "");
  tempMessageEndTime = millis() + 2000;
  updateDisplay();
}

// ============================================
// DISPLAY UPDATE
// ============================================
void updateDisplay() {
  if (!displayReady) {
    return;
  }

  static unsigned long lastUpdateMs = 0;
  if (millis() - lastUpdateMs < 100) {
    return;
  }
  lastUpdateMs = millis();

  if (millis() < tempMessageEndTime && tempMessageLine1[0] != '\0') {
    char balanceLine[32];
    formatBalanceLine(balanceLine, sizeof(balanceLine));
    renderScreen(tempMessageLine1, tempMessageLine2, balanceLine,
                 networkStatusLine);
    return;
  }

  if (millis() >= tempMessageEndTime) {
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
  case FREE_WATER:
    displayFreeWater();
    break;
  default:
    renderStateScreen("HOLAT XATO", "Qayta ishga tushiring", nullptr);
    break;
  }
}

// ============================================
// STATE DISPLAYS (optimized for 20x4)
// ============================================
void displayIdle() {
  const bool freeOffer = (config.enableFreeWater && !freeWaterUsed &&
                          millis() >= freeWaterAvailableTime);
  const char *detail = freeOffer ? "START: bepul suv" : "Tolovni kiriting";
  renderStateScreen("HOLAT: KUTISH", detail, nullptr);
}

void displayActive() {
  renderStateScreen("HOLAT: TAYYOR", "START ni bosing", nullptr);
}

void displayDispensing() {
  char litersLine[32];
  formatDispensedLine(litersLine, sizeof(litersLine), totalDispensedLiters);

  const bool showStopHint = ((millis() / 1000UL) % 2UL) == 1UL;
  renderStateScreen("HOLAT: QUYILMOQDA", litersLine,
                    showStopHint ? "PAUSE: to'xtatish" : nullptr);
}

void displayFreeWater() {
  char freeLine[32];
  formatFreeWaterLine(freeLine, sizeof(freeLine));

  const bool showStopHint = ((millis() / 1000UL) % 2UL) == 1UL;
  renderStateScreen("HOLAT: BEPUL SUV", freeLine,
                    showStopHint ? "PAUSE: to'xtatish" : nullptr);
}

void displayPaused() {
  renderStateScreen("HOLAT: PAUZA", "START: davom eting", nullptr);
}

bool isDisplayReady() { return displayReady; }
