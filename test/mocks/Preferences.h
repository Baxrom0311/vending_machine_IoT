#ifndef PREFERENCES_MOCK_H
#define PREFERENCES_MOCK_H

#include <Arduino.h>
#include <map>
#include <string>

class Preferences {
public:
  std::map<std::string, std::string> _storage;
  std::map<std::string, std::string> _bytes;
  std::map<std::string, bool> _bools;
  std::map<std::string, int> _ints;
  std::map<std::string, unsigned long> _ulongs;
  std::map<std::string, float> _floats;
  std::map<std::string, uint8_t> _uchars;

  bool _begun = false;

  bool begin(const char *name, bool readOnly = false) {
    _begun = true;
    return true;
  }

  void end() { _begun = false; }

  void clear() {
    _storage.clear();
    _bytes.clear();
    _bools.clear();
    _ints.clear();
    _ulongs.clear();
    _floats.clear();
    _uchars.clear();
  }

  // String
  size_t putString(const char *key, const char *value) {
    _storage[key] = value;
    return strlen(value);
  }
  String getString(const char *key, const String defaultValue = String()) {
    if (_storage.find(key) != _storage.end()) {
      return String(_storage[key].c_str());
    }
    return defaultValue;
  }

  // Bool
  size_t putBool(const char *key, bool value) {
    _bools[key] = value;
    return 1;
  }
  bool getBool(const char *key, bool defaultValue = false) {
    if (_bools.find(key) != _bools.end()) {
      return _bools[key];
    }
    return defaultValue;
  }

  // Int
  size_t putInt(const char *key, int32_t value) {
    _ints[key] = value;
    return 4;
  }
  int32_t getInt(const char *key, int32_t defaultValue = 0) {
    if (_ints.find(key) != _ints.end()) {
      return _ints[key];
    }
    return defaultValue;
  }

  // ULong
  size_t putULong(const char *key, uint32_t value) {
    _ulongs[key] = value;
    return 4;
  }
  uint32_t getULong(const char *key, uint32_t defaultValue = 0) {
    if (_ulongs.find(key) != _ulongs.end()) {
      return _ulongs[key];
    }
    return defaultValue;
  }

  // Float
  size_t putFloat(const char *key, float value) {
    _floats[key] = value;
    return 4;
  }
  float getFloat(const char *key, float defaultValue = NAN) {
    if (_floats.find(key) != _floats.end()) {
      return _floats[key];
    }
    return defaultValue;
  }

  // Bytes
  size_t putBytes(const char *key, const void *value, size_t len) {
    _bytes[key] = std::string(reinterpret_cast<const char *>(value), len);
    return len;
  }
  size_t getBytes(const char *key, void *buffer, size_t maxLen) {
    auto it = _bytes.find(key);
    if (it == _bytes.end()) {
      return 0;
    }
    size_t n = it->second.size();
    if (n > maxLen) {
      n = maxLen;
    }
    memcpy(buffer, it->second.data(), n);
    return n;
  }

  // UChar
  size_t putUChar(const char *key, uint8_t value) {
    _uchars[key] = value;
    return 1;
  }
  uint8_t getUChar(const char *key, uint8_t defaultValue = 0) {
    auto it = _uchars.find(key);
    if (it == _uchars.end()) {
      return defaultValue;
    }
    return it->second;
  }
};

extern Preferences preferences;

#endif
