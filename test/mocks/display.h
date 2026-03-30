#ifndef DISPLAY_H
#define DISPLAY_H

#include "Arduino.h"

#ifndef LCD_COLS
#define LCD_COLS 20
#endif

#ifndef LCD_ROWS
#define LCD_ROWS 4
#endif

// Mock LCD class for display linkage in tests
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

void initDisplay();
void updateDisplay();
void displayIdle();
void displayActive();
void displayDispensing();
void displayPaused();
void showTemporaryMessage(const char *line1, const char *line2);
void setDisplayNetworkStatus(const char *message);
void scheduleDisplayReinit(unsigned long quietMs = 150UL);
bool isDisplayReady();
void displayStatus();
void displayError(const char *msg);

#endif
