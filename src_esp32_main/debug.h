#ifndef DEBUG_H
#define DEBUG_H

// Conditional debug logging system
// Set ENABLE_DEBUG_LOGS=0 in platformio.ini for production builds

#ifndef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 1 // Default: enabled for development
#endif

#if ENABLE_DEBUG_LOGS
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(fmt, ...)
#endif

#endif
