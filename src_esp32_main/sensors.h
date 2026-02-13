#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// ============================================
// TDS SENSOR
// ============================================
extern int tdsPPM;

void initSensors();
int readTDS();
void publishTDS();

// ============================================
// FLOW SENSOR ISR
// ============================================
void flowSensorISR();

#endif
