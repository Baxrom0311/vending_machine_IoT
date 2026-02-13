#ifndef DISPLAY_H
#define DISPLAY_H

#include "Arduino.h"

// Mock LCD class (used directly in mqtt_handler identify command)
class LiquidCrystal_I2C {
public:
  LiquidCrystal_I2C() {}
  void init() {}
  void backlight() {}
  void noBacklight() {}
  void clear() {}
  void setCursor(int col, int row) {}
  void print(const char *text) {}
  void print(const String &text) {}
};

extern LiquidCrystal_I2C lcd;

// Mock TFT class
class TFT_eSPI {
public:
  void fillScreen(uint32_t color) {}
  void setCursor(int16_t x, int16_t y) {}
  void setTextColor(uint16_t color) {}
  void setTextSize(uint8_t size) {}
  void print(const char *text) {}
  void print(int n) {}
  void print(float f) {}
};

// Colors
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_CYAN 0x07FF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0

extern TFT_eSPI tft;

void initDisplay();
void updateDisplay();
void displayIdle();
void displayActive();
void displayDispensing();
void displayFreeWater();
void displayPaused();
void showTemporaryMessage(const char *line1, const char *line2);
void displayStatus();
void displayError(const char *msg);

#endif
