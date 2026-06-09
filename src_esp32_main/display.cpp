#include "display.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include "relay_control.h"
#include "state_machine.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <cstdio>

namespace {
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t TEXT_SIZE = 1;
constexpr uint8_t LINE_HEIGHT = 16;
constexpr unsigned long DISPLAY_REFRESH_MS = 250;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool displayReady = false;
unsigned long lastDisplayRefreshMs = 0;
unsigned long messageUntilMs = 0;
char messageLine1[22] = {0};
char messageLine2[22] = {0};

const char *stateName(SystemState state) {
  switch (state) {
  case IDLE:
    return "IDLE";
  case ACTIVE:
    return "ACTIVE";
  case DISPENSING:
    return "RUN";
  case PAUSED:
    return "PAUSE";
  default:
    return "?";
  }
}

void drawLine(uint8_t line, const char *text) {
  display.setCursor(0, line * LINE_HEIGHT);
  display.print(text ? text : "");
}

void renderStatus() {
  char line[32];

  display.clearDisplay();
  display.setTextSize(TEXT_SIZE);
  display.setTextColor(SSD1306_WHITE);

  snprintf(line, sizeof(line), "HOLAT: %s", stateName(currentState));
  drawLine(0, line);

  snprintf(line, sizeof(line), "BAL: %ld", balance);
  drawLine(1, line);

  snprintf(line, sizeof(line), "NARX: %d/L", config.pricePerLiter);
  drawLine(2, line);

  if (currentState == DISPENSING || currentState == PAUSED ||
      currentState == ACTIVE) {
    const unsigned long elapsed = millis() - lastSessionActivity;
    const unsigned long remainMs =
        elapsed >= config.sessionTimeout ? 0 : (config.sessionTimeout - elapsed);
    snprintf(line, sizeof(line), "R1:%s R2:%s T:%lus", isRelayOn() ? "1" : "0",
             isRelay2On() ? "1" : "0", remainMs / 1000UL);
  } else {
    snprintf(line, sizeof(line), "R1:%s R2:%s", isRelayOn() ? "1" : "0",
             isRelay2On() ? "1" : "0");
  }
  drawLine(3, line);

  display.display();
}

void renderMessage() {
  display.clearDisplay();
  display.setTextSize(TEXT_SIZE);
  display.setTextColor(SSD1306_WHITE);
  drawLine(1, messageLine1);
  drawLine(2, messageLine2);
  display.display();
}
} // namespace

void initDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DEBUG_PRINTLN("OLED init failed");
    displayReady = false;
    return;
  }

  displayReady = true;
  display.clearDisplay();
  display.setTextWrap(false);
  showTemporaryMessage("eWater", "Starting...");
  DEBUG_PRINTLN("OLED ready");
}

void updateDisplay() {
  if (!displayReady) {
    return;
  }

  const unsigned long now = millis();
  if (messageUntilMs != 0 && now < messageUntilMs) {
    renderMessage();
    return;
  }

  messageUntilMs = 0;
  if ((now - lastDisplayRefreshMs) < DISPLAY_REFRESH_MS) {
    return;
  }

  lastDisplayRefreshMs = now;
  renderStatus();
}

void showTemporaryMessage(const char *line1, const char *line2) {
  if (!displayReady) {
    return;
  }

  snprintf(messageLine1, sizeof(messageLine1), "%s", line1 ? line1 : "");
  snprintf(messageLine2, sizeof(messageLine2), "%s", line2 ? line2 : "");
  messageUntilMs = millis() + 1200UL;
  renderMessage();
}
