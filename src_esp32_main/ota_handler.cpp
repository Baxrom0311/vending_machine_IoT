#include "ota_handler.h"
#include "config.h"
#include "config_storage.h"
#include "mqtt_handler.h"
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

// ============================================
// OTA SETUP
// ============================================
void setupOTA() {
  if (!WiFi.isConnected()) {
    Serial.println("OTA: WiFi not connected, skipping setup");
    return;
  }

  // Set OTA hostname
  String hostname = "ewater-";
  hostname += deviceConfig.device_id;
  ArduinoOTA.setHostname(hostname.c_str());

  // Set OTA password (using API secret)
  if (deviceConfig.api_secret[0] != '\0') {
    ArduinoOTA.setPassword(deviceConfig.api_secret);
  }

  // OTA callbacks
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("OTA: Start updating " + type);
    publishLog("OTA", ("Started: " + type).c_str());
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA: Update complete!");
    publishLog("OTA", "Update complete, rebooting...");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // OTA can run longer than the main loop watchdog window
    esp_task_wdt_reset();

    static unsigned long lastReport = 0;
    unsigned long now = millis();

    // Report every 5 seconds
    if (now - lastReport > 5000) {
      int percent = (progress * 100) / total;
      Serial.printf("OTA Progress: %u%%\r", percent);

      // Publish progress to MQTT
      char msg[32];
      snprintf(msg, sizeof(msg), "Progress: %d%%", percent);
      publishLog("OTA", msg);

      lastReport = now;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    const char *errMsg = "Unknown error";
    if (error == OTA_AUTH_ERROR)
      errMsg = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR)
      errMsg = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR)
      errMsg = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR)
      errMsg = "Receive Failed";
    else if (error == OTA_END_ERROR)
      errMsg = "End Failed";

    Serial.println(errMsg);
    publishLog("OTA_ERROR", errMsg);
  });

  ArduinoOTA.begin();
  Serial.println("OTA: Ready");
  Serial.print("OTA: Hostname: ");
  Serial.println(hostname);

  publishLog("OTA", ("Ready: " + hostname).c_str());
}

// ============================================
// OTA HANDLE (Call in loop)
// ============================================
void handleOTA() { ArduinoOTA.handle(); }

// ============================================
// TRIGGER OTA UPDATE FROM URL (via MQTT)
// ============================================
void triggerOTAUpdate(const char *firmwareUrl) {
  Serial.println("OTA: Starting HTTP update...");
  Serial.print("URL: ");
  Serial.println(firmwareUrl);

  publishLog("OTA", "Starting HTTP update...");

  HTTPClient http;
  http.begin(firmwareUrl);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("OTA: HTTP error: %d\n", httpCode);
    publishLog("OTA_ERROR", "HTTP download failed");
    http.end();
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("OTA: Invalid content length");
    publishLog("OTA_ERROR", "Invalid firmware size");
    http.end();
    return;
  }

  Serial.printf("OTA: Firmware size: %d bytes\n", contentLength);

  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Serial.println("OTA: Not enough space");
    publishLog("OTA_ERROR", "Not enough flash space");
    http.end();
    return;
  }

  // Get stream
  WiFiClient *stream = http.getStreamPtr();

  // Write firmware
  size_t written = 0;
  uint8_t buff[128];
  unsigned long lastReport = 0;

  while (http.connected() && written < contentLength) {
    esp_task_wdt_reset();

    size_t available = stream->available();
    if (available) {
      int c = stream->readBytes(buff, min(available, sizeof(buff)));
      written += Update.write(buff, c);

      // Report progress every 5 seconds
      if (millis() - lastReport > 5000) {
        int percent = (written * 100) / contentLength;
        Serial.printf("OTA: %d%%\r", percent);

        char msg[32];
        snprintf(msg, sizeof(msg), "Progress: %d%%", percent);
        publishLog("OTA", msg);

        lastReport = millis();
      }
    }
    delay(1);
  }

  if (Update.end()) {
    Serial.println("\nOTA: Update success!");
    if (Update.isFinished()) {
      Serial.println("OTA: Rebooting...");
      publishLog("OTA", "Update complete, rebooting...");
      delay(1000);
      ESP.restart();
    } else {
      Serial.println("OTA: Update not finished");
      publishLog("OTA_ERROR", "Update incomplete");
    }
  } else {
    Serial.printf("OTA: Error: %s\n", Update.errorString());
    publishLog("OTA_ERROR", Update.errorString());
  }

  http.end();
}
