#include "display.h"
#include "config.h"
#include "hardware.h"
#include "state_machine.h"
#include <Wire.h>
#include <cstdio>
#include <cstring>

// ============================================
// LCD OBJECT (16x2, I2C address from build flags)
// ============================================
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// ============================================
// TEMPORARY MESSAGE (line 1 only)
// ============================================
static char tempMessageLine1[LCD_COLS + 1] = {0};
static unsigned long tempMessageEndTime = 0;

// ============================================
// RENDER CACHE
// ============================================
static char lastLine0[LCD_COLS + 1] = {0};
static char lastLine1[LCD_COLS + 1] = {0};

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

static void writeLineCached(uint8_t row, const char *text) {
  char fixed[LCD_COLS + 1];
  toFixedWidth(text, fixed, sizeof(fixed));

  char *cache = (row == 0) ? lastLine0 : lastLine1;
  if (strcmp(cache, fixed) == 0) {
    return;
  }

  lcd.setCursor(0, row);
  lcd.print(fixed);
  strcpy(cache, fixed);
}

static void formatBalanceLine(char *out, size_t outSize) {
  if (balance > 999999999L) {
    snprintf(out, outSize, "Balans:999999999");
    return;
  }
  if (balance < -99999999L) {
    snprintf(out, outSize, "Balans:-99999999");
    return;
  }
  snprintf(out, outSize, "Balans:%ld", balance);
}

static void renderStateLine(const char *stateText) {
  char balanceLine[24];
  formatBalanceLine(balanceLine, sizeof(balanceLine));

  writeLineCached(0, stateText);
  writeLineCached(1, balanceLine);
}

// ============================================
// INITIALIZATION
// ============================================
void initDisplay() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  memset(lastLine0, 0, sizeof(lastLine0));
  memset(lastLine1, 0, sizeof(lastLine1));

  writeLineCached(0, "TIZIM YUKLANMOQDA");
  writeLineCached(1, "Balans:0");
}

// ============================================
// TEMPORARY MESSAGE
// ============================================
void showTemporaryMessage(const char *line1, const char *line2) {
  (void)line2;
  toFixedWidth(line1, tempMessageLine1, sizeof(tempMessageLine1));
  tempMessageEndTime = millis() + 2000;
  updateDisplay();
}

// ============================================
// DISPLAY UPDATE
// ============================================
void updateDisplay() {
  static unsigned long lastUpdateMs = 0;
  if (millis() - lastUpdateMs < 100) {
    return;
  }
  lastUpdateMs = millis();

  if (millis() < tempMessageEndTime && tempMessageLine1[0] != '\0') {
    renderStateLine(tempMessageLine1);
    return;
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
    renderStateLine("HOLAT XATO");
    break;
  }
}

// ============================================
// STATE DISPLAYS (line 1 max 16 chars)
// ============================================
void displayIdle() {
  const bool freeOffer = (config.enableFreeWater && !freeWaterUsed &&
                          millis() >= freeWaterAvailableTime);
  renderStateLine(freeOffer ? "START BEPUL SUV" : "PUL KIRITING");
}

void displayActive() { renderStateLine("START BOSING"); }

void displayDispensing() {
  const bool showStopHint = ((millis() / 1000UL) % 2UL) == 1UL;
  renderStateLine(showStopHint ? "PAUSE=STOP" : "SUV QUYILMOQDA");
}

void displayFreeWater() {
  const bool showStopHint = ((millis() / 1000UL) % 2UL) == 1UL;
  renderStateLine(showStopHint ? "PAUSE=STOP" : "BEPUL SUV OQAR");
}

void displayPaused() { renderStateLine("PAUZA: START"); }
