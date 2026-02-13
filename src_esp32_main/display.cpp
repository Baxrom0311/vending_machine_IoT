#include "display.h"
#include "config.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "sensors.h"
#include "state_machine.h"
#include <WiFi.h>
#include <Wire.h>

// ============================================
// LCD OBJECT (20x4, I2C address 0x27)
// ============================================
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// ============================================
// CUSTOM CHARACTERS
// ============================================
// Progress bar characters
byte progressFull[8] = {0b11111, 0b11111, 0b11111, 0b11111,
                        0b11111, 0b11111, 0b11111, 0b11111};

byte progressEmpty[8] = {0b11111, 0b10001, 0b10001, 0b10001,
                         0b10001, 0b10001, 0b10001, 0b11111};

// WiFi icon
byte wifiIcon[8] = {0b00000, 0b01110, 0b10001, 0b00100,
                    0b01010, 0b00000, 0b00100, 0b00000};

// No WiFi icon
byte noWifiIcon[8] = {0b00001, 0b01110, 0b10011, 0b00100,
                      0b01110, 0b00100, 0b00100, 0b00000};

// ============================================
// INITIALIZATION
// ============================================
void initDisplay() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  lcd.init();
  lcd.backlight();

  // Create custom characters
  lcd.createChar(0, progressFull);
  lcd.createChar(1, progressEmpty);
  lcd.createChar(2, wifiIcon);
  lcd.createChar(3, noWifiIcon);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TOZA SUV AVTOMATI");
  lcd.setCursor(0, 1);
  lcd.print("Yuklanmoqda...");
}

// ============================================
// HELPER FUNCTIONS
// ============================================
static void clearLine(int row) {
  lcd.setCursor(0, row);
  for (int i = 0; i < LCD_COLS; i++) {
    lcd.print(' ');
  }
}

static void printCentered(int row, const char *text) {
  int len = strlen(text);
  int pos = (LCD_COLS - len) / 2;
  if (pos < 0)
    pos = 0;
  clearLine(row);
  lcd.setCursor(pos, row);
  lcd.print(text);
}

static void drawProgressBar(int row, int percent) {
  lcd.setCursor(0, row);
  lcd.print('[');

  int barWidth = LCD_COLS - 7; // "[" + "] XXX%" = 7 chars
  int filled = (percent * barWidth) / 100;

  for (int i = 0; i < barWidth; i++) {
    if (i < filled) {
      lcd.write(byte(0)); // Full block
    } else {
      lcd.print('-');
    }
  }

  lcd.print(']');

  // Print percentage
  char pctBuf[5];
  snprintf(pctBuf, sizeof(pctBuf), "%3d%%", percent);
  lcd.print(pctBuf);
}

static void drawStatusLine() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  bool mqttOk = mqttClient.connected();

  lcd.setCursor(0, 3);

  // TDS info
  char tdsBuf[12];
  snprintf(tdsBuf, sizeof(tdsBuf), "TDS:%3dppm", tdsPPM);
  lcd.print(tdsBuf);

  // Spacer
  lcd.print("  ");

  // WiFi status
  if (wifiOk) {
    lcd.write(byte(2)); // WiFi icon
    lcd.print("OK");
  } else {
    lcd.write(byte(3)); // No WiFi icon
    lcd.print("--");
  }

  // MQTT status
  lcd.print(" M:");
  lcd.print(mqttOk ? "OK" : "--");
}

// ============================================
// TEMPORARY MESSAGE
// ============================================
static char tempMessageLine1[21];
static char tempMessageLine2[21];
static unsigned long tempMessageEndTime = 0;

void showTemporaryMessage(const char *line1, const char *line2) {
  strncpy(tempMessageLine1, line1, sizeof(tempMessageLine1));
  tempMessageLine1[sizeof(tempMessageLine1) - 1] = 0;

  strncpy(tempMessageLine2, line2, sizeof(tempMessageLine2));
  tempMessageLine2[sizeof(tempMessageLine2) - 1] = 0;

  tempMessageEndTime = millis() + 2000; // Show for 2 seconds
  updateDisplay();                      // Trigger immediate update
}

// ============================================
// DISPLAY UPDATE
// ============================================
void updateDisplay() {
  static SystemState lastState = IDLE;
  static long lastBalance = LONG_MIN;
  static int lastDispensed100 = -1;
  static int lastFreeMl = -1;
  static bool lastFreeOffer = false;
  static int lastTimeoutSec = -1;
  static int lastTds = -1;
  static bool lastWifiOk = false;
  static bool lastMqttOk = false;
  static unsigned long lastUpdateMs = 0;

  // Temporary message handling
  static bool wasShowingMessage = false;
  static bool forceRedraw = false;
  if (millis() < tempMessageEndTime) {
    if (!wasShowingMessage) {
      lcd.clear();
      wasShowingMessage = true;
    }
    printCentered(1, tempMessageLine1);
    printCentered(2, tempMessageLine2);
    return; // Block other updates
  } else if (wasShowingMessage) {
    lcd.clear();
    wasShowingMessage = false;
    forceRedraw = true; // Force full redraw after temp message
  }

  // Throttle updates to max 5 per second
  if (millis() - lastUpdateMs < 200) {
    return;
  }

  const bool wifiOk = (WiFi.status() == WL_CONNECTED);
  const bool mqttOk = mqttClient.connected();
  const int dispensed100 = (int)(totalDispensedLiters * 100.0f + 0.5f);
  const int freeMl = (int)(freeWaterDispensed * 1000.0f + 0.5f);
  const bool freeOffer = (config.enableFreeWater && currentState == IDLE &&
                          !freeWaterUsed && millis() >= freeWaterAvailableTime);

  int timeoutSec = -1;
  if (currentState == ACTIVE || currentState == PAUSED) {
    unsigned long elapsed = millis() - lastSessionActivity;
    unsigned long timeLeft = (elapsed >= config.sessionTimeout)
                                 ? 0
                                 : (config.sessionTimeout - elapsed);
    timeoutSec = (int)(timeLeft / 1000);
  }

  // Check if anything changed
  bool stateChanged = (currentState != lastState);
  bool balanceChanged = (balance != lastBalance);
  bool dispensedChanged = (dispensed100 != lastDispensed100);
  bool freeChanged = (freeMl != lastFreeMl);
  bool offerChanged = (freeOffer != lastFreeOffer);
  bool timeoutChanged = (timeoutSec != lastTimeoutSec);
  bool tdsChanged = (tdsPPM != lastTds);
  bool wifiChanged = (wifiOk != lastWifiOk || mqttOk != lastMqttOk);

  bool needUpdate = stateChanged || balanceChanged || dispensedChanged ||
                    freeChanged || offerChanged || timeoutChanged ||
                    tdsChanged || wifiChanged || forceRedraw;

  if (!needUpdate) {
    return;
  }

  lastUpdateMs = millis();

  // Full redraw on state change or forced redraw
  if (stateChanged || forceRedraw) {
    lcd.clear();
    forceRedraw = false;
  }

  // Draw state-specific content
  switch (currentState) {
  case IDLE:
    displayIdle();
    break;
  case ACTIVE:
    displayActive();
    break;
  case PAUSED:
    displayPaused();
    break;
  case DISPENSING:
    displayDispensing();
    break;
  case FREE_WATER:
    displayFreeWater();
    break;
  }

  // Always draw status line
  drawStatusLine();

  // Update cached values
  lastState = currentState;
  lastBalance = balance;
  lastDispensed100 = dispensed100;
  lastFreeMl = freeMl;
  lastFreeOffer = freeOffer;
  lastTimeoutSec = timeoutSec;
  lastTds = tdsPPM;
  lastWifiOk = wifiOk;
  lastMqttOk = mqttOk;
}

// ============================================
// STATE DISPLAYS
// ============================================

void displayIdle() {
  bool freeOffer = (config.enableFreeWater && !freeWaterUsed &&
                    millis() >= freeWaterAvailableTime);

  if (freeOffer) {
    // Free water available
    printCentered(0, "TOZA SUV AVTOMATI");
    printCentered(1, ">>> BEPUL 200ml! <<<");
    printCentered(2, "START bosing");
  } else {
    // Normal idle
    printCentered(0, "TOZA SUV AVTOMATI");

    lcd.setCursor(0, 1);
    lcd.print("Balans: 0 so'm");

    printCentered(2, "Pul kiriting...");
  }
}

void displayActive() {
  // Line 0: Balance
  lcd.setCursor(0, 0);
  char buf[21];
  snprintf(buf, sizeof(buf), "Balans: %ld so'm", balance);
  lcd.print(buf);
  // Clear rest of line
  for (int i = strlen(buf); i < LCD_COLS; i++)
    lcd.print(' ');

  // Line 1: Dispensed
  lcd.setCursor(0, 1);
  snprintf(buf, sizeof(buf), "Quyildi: %.2fL", totalDispensedLiters);
  lcd.print(buf);
  for (int i = strlen(buf); i < LCD_COLS; i++)
    lcd.print(' ');

  // Line 2: Action hint
  printCentered(2, "START = Boshlash");
}

void displayPaused() {
  printCentered(0, "=== PAUZA ===");

  // Line 1: Balance
  lcd.setCursor(0, 1);
  char buf[21];
  snprintf(buf, sizeof(buf), "Balans: %ld so'm", balance);
  lcd.print(buf);
  for (int i = strlen(buf); i < LCD_COLS; i++)
    lcd.print(' ');

  // Line 2: Action hint
  printCentered(2, "START = Davom");
}

void displayDispensing() {
  // Line 0: Status with animation
  static int animFrame = 0;
  const char *anim[] = {">  SUV QUYILMOQDA", ">> SUV QUYILMOQDA",
                        ">>>SUV QUYILMOQDA"};
  animFrame = (animFrame + 1) % 3;

  lcd.setCursor(0, 0);
  lcd.print(anim[animFrame]);
  for (int i = strlen(anim[animFrame]); i < LCD_COLS; i++)
    lcd.print(' ');

  // Line 1: Dispensed amount
  lcd.setCursor(0, 1);
  char buf[21];
  snprintf(buf, sizeof(buf), "Quyildi: %.2f L", totalDispensedLiters);
  lcd.print(buf);
  for (int i = strlen(buf); i < LCD_COLS; i++)
    lcd.print(' ');

  // Line 2: Balance
  lcd.setCursor(0, 2);
  snprintf(buf, sizeof(buf), "Balans: %ld so'm", balance);
  lcd.print(buf);
  for (int i = strlen(buf); i < LCD_COLS; i++)
    lcd.print(' ');
}

void displayFreeWater() {
  printCentered(0, "*** BEPUL SUV ***");

  // Line 1: Progress in ml
  float targetMl = config.freeWaterAmount * 1000;
  float currentMl = freeWaterDispensed * 1000;

  lcd.setCursor(0, 1);
  char buf[21];
  snprintf(buf, sizeof(buf), "%.0f / %.0f ml", currentMl, targetMl);
  // Center it
  int len = strlen(buf);
  int pos = (LCD_COLS - len) / 2;
  clearLine(1);
  lcd.setCursor(pos, 1);
  lcd.print(buf);

  // Line 2: Progress bar
  int percent = 0;
  if (config.freeWaterAmount > 0) {
    percent = (int)((freeWaterDispensed / config.freeWaterAmount) * 100);
    if (percent < 0)
      percent = 0;
    if (percent > 100)
      percent = 100;
  }
  drawProgressBar(2, percent);
}
