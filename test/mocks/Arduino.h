#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

typedef uint8_t byte;
typedef bool boolean;

// String Mock
class String {
public:
  std::string internal;

  String() : internal("") {}
  String(const char *s) : internal(s ? s : "") {}
  String(const std::string &s) : internal(s) {}
  String(int v) : internal(std::to_string(v)) {}
  String(unsigned int v) : internal(std::to_string(v)) {}
  String(long v) : internal(std::to_string(v)) {}
  String(unsigned long v) : internal(std::to_string(v)) {}
  String(float v) : internal(std::to_string(v)) {}
  String(double v) : internal(std::to_string(v)) {}

  const char *c_str() const { return internal.c_str(); }
  size_t length() const { return internal.length(); }

  // Operators
  String &operator=(const char *s) {
    internal = s ? s : "";
    return *this;
  }
  String &operator=(const String &s) {
    internal = s.internal;
    return *this;
  }

  String &operator+=(const char *s) {
    internal += (s ? s : "");
    return *this;
  }
  String &operator+=(char c) {
    internal += c;
    return *this;
  }
  String &operator+=(const String &s) {
    internal += s.internal;
    return *this;
  }
  String &operator+=(int v) {
    internal += std::to_string(v);
    return *this;
  }

  bool operator==(const char *s) const { return internal == (s ? s : ""); }
  bool operator==(const String &s) const { return internal == s.internal; }
  bool operator!=(const char *s) const { return internal != (s ? s : ""); }
  bool operator!=(const String &s) const { return internal != s.internal; }

  String operator+(const char *s) const {
    return String(internal + (s ? s : ""));
  }
  String operator+(const String &s) const {
    return String(internal + s.internal);
  }

  char operator[](unsigned int index) const { return internal[index]; }

  // Methods
  void reserve(unsigned int size) { internal.reserve(size); }
  unsigned char concat(const char *s) {
    *this += s;
    return 1;
  }
  unsigned char concat(const String &s) {
    *this += s;
    return 1;
  }
  unsigned char concat(char c) {
    *this += c;
    return 1;
  }
  void toLowerCase() {
    for (auto &c : internal)
      c = tolower(c);
  }
};

// Serial Mock
class SerialMock {
public:
  // Print overloads
  virtual size_t print(const char *s) { return strlen(s); }
  virtual size_t print(const String &s) { return s.length(); }
  virtual size_t print(int v) { return 1; }
  virtual size_t print(unsigned int v) { return 1; }
  virtual size_t print(long v) { return 1; }
  virtual size_t print(unsigned long v) { return 1; }
  virtual size_t print(double v, int d = 2) { return 1; }
  virtual size_t print(float v, int d = 2) { return 1; } // Explicit float

  // Println overloads
  virtual size_t println(const char *s) { return strlen(s) + 1; }
  virtual size_t println(const String &s) { return s.length() + 1; }
  virtual size_t println(int v) { return 1; }
  virtual size_t println(unsigned int v) { return 1; }
  virtual size_t println(long v) { return 1; }
  virtual size_t println(unsigned long v) { return 1; }
  virtual size_t println(double v, int d = 2) { return 1; }
  virtual size_t println(float v, int d = 2) { return 1; } // Explicit float
  virtual size_t println() { return 1; }                   // Empty println
};

extern SerialMock Serial;

// Time functions
extern unsigned long _millis_mock;
inline unsigned long millis() { return _millis_mock; }
inline void delay(unsigned long ms) { _millis_mock += ms; }

// Utils
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// GPIO
#define INPUT 0x0
#define OUTPUT 0x1
#define LOW 0x0
#define HIGH 0x1
#define INPUT_PULLUP 0x2
#define IRAM_ATTR

// Simple mock for digitalRead/Write (we can track state if needed, but for now
// stubs)
inline void pinMode(uint8_t pin, uint8_t mode) {}
inline void digitalWrite(uint8_t pin, uint8_t val) {}
inline int digitalRead(uint8_t pin) { return LOW; }
inline void noInterrupts() {}
inline void interrupts() {}

// Global String operator for "char*" + String
inline String operator+(const char *lhs, const String &rhs) {
  return String(lhs) + rhs;
}

// ESP Mock
class EspClass {
public:
  void restart() {}
};
extern EspClass ESP;

#endif
