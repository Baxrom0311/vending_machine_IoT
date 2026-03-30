#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_ST7789.h>

// ============================================
// TFT OBJECT
// ============================================
extern Adafruit_ST7789 lcd;

// ============================================
// FUNCTIONS
// ============================================
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

#endif
