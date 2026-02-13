#ifndef DISPLAY_H
#define DISPLAY_H

#include <LiquidCrystal_I2C.h>

// ============================================
// LCD CONFIGURATION
// ============================================
#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x27
#endif

#ifndef LCD_COLS
#define LCD_COLS 16
#endif

#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif

// ============================================
// LCD OBJECT
// ============================================
extern LiquidCrystal_I2C lcd;

// ============================================
// FUNCTIONS
// ============================================
void initDisplay();
void updateDisplay();
void displayIdle();
void displayActive();
void displayDispensing();
void displayFreeWater();
void displayPaused();
void showTemporaryMessage(const char *line1, const char *line2);

#endif
