#include "sensors.h"
#include "config.h"
#include "config_storage.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "state_machine.h"
#include <ArduinoJson.h>

// ============================================
// GLOBAL VARIABLES
// ============================================
int tdsPPM = 0;

// ============================================
// INITIALIZATION
// ============================================
void initSensors() {
  pinMode(TDS_PIN, INPUT);
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR,
                  RISING);
}

// ============================================
// FLOW SENSOR ISR
// ============================================
void IRAM_ATTR flowSensorISR() { flowPulseCount++; }

// ============================================
// TDS SENSOR
// ============================================
int readTDS() {
  int sensorValue = analogRead(TDS_PIN);

  // Convert to voltage (ESP32 ADC: 0-4095 = 0-3.3V)
  float voltage = sensorValue * (3.3 / 4095.0);

  // TDS formula (varies by sensor, calibrate!)
  // Example for TDS Meter V1.0:
  // Constants based on standard TDS curve
  const float TDS_FACTOR_A = 133.42;
  const float TDS_FACTOR_B = 255.86;
  const float TDS_FACTOR_C = 857.39;

  float temperature = config.tdsTemperatureC;
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  float compensationVoltage = voltage / compensationCoefficient;

  float rawTds = (TDS_FACTOR_A * compensationVoltage * compensationVoltage *
                      compensationVoltage -
                  TDS_FACTOR_B * compensationVoltage * compensationVoltage +
                  TDS_FACTOR_C * compensationVoltage);

  // FIX: Validate calibration factor to prevent zero/invalid values
  float calibrationFactor = config.tdsCalibrationFactor;
  if (calibrationFactor <= 0.0f || calibrationFactor > 10.0f) {
    Serial.println(
        "WARNING: Invalid TDS calibration factor, using default 1.0");
    calibrationFactor = 1.0f; // Safe default
  }

  float tds = rawTds * calibrationFactor;

  int tdsInt = (int)tds;
  // analytics reference removed

  return tdsInt;
}

// ============================================
// PUBLISH TDS
// ============================================
void publishTDS() {
  JsonDocument doc;

  doc["device_id"] = deviceConfig.device_id;
  doc["tds"] = tdsPPM;

  String output;
  serializeJson(doc, output);

  publishMQTT(TOPIC_TDS_OUT, output.c_str());
}
