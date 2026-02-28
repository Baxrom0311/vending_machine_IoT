#include "sensors.h"
#include "config.h"
#include "config_storage.h"
#include "debug.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "relay_control.h"
#include "state_machine.h"
#include <ArduinoJson.h>

// ============================================
// GLOBAL VARIABLES
// ============================================
int tdsPPM = 0;
static volatile unsigned long lastAcceptedFlowPulseUs = 0;

// Reject implausibly fast pulses and relay switching noise.
// For ~450 pulses/L sensors this still allows >20L/min flow safely.
static constexpr unsigned long FLOW_MIN_PULSE_INTERVAL_US = 2500UL;
static constexpr unsigned long FLOW_RELAY_NOISE_GUARD_US = 250000UL;

// ============================================
// INITIALIZATION
// ============================================
void initSensors() {
  pinMode(TDS_PIN, INPUT);
#if (FLOW_SENSOR_PIN >= 34 && FLOW_SENSOR_PIN <= 39)
  // GPIO34-39 on ESP32 do not support internal pull-up/down.
  // External 4.7k-10k pull-up to 3.3V is required for stable flow input.
  pinMode(FLOW_SENSOR_PIN, INPUT);
  DEBUG_PRINTLN("Flow sensor: external pull-up required on this GPIO");
#else
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
#endif

  lastAcceptedFlowPulseUs = 0;
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR,
                  RISING);
}

// ============================================
// FLOW SENSOR ISR
// ============================================
void IRAM_ATTR flowSensorISR() {
  const unsigned long nowUs = micros();

  // Ignore spikes shortly after relay transitions (AC valve EMI).
  if ((nowUs - getRelayLastChangeUs()) < FLOW_RELAY_NOISE_GUARD_US) {
    return;
  }

  // Ignore unrealistically fast pulse bursts.
  if ((nowUs - lastAcceptedFlowPulseUs) < FLOW_MIN_PULSE_INTERVAL_US) {
    return;
  }

  lastAcceptedFlowPulseUs = nowUs;
  flowPulseCount++;
}

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
    DEBUG_PRINTLN(
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

  char output[128];
  size_t outLen = serializeJson(doc, output, sizeof(output));
  if (outLen == 0) {
    return;
  }
  publishMQTT(TOPIC_TDS_OUT, output);
}
