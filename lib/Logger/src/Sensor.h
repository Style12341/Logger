#pragma once // Remove redundant #ifndef SENSOR_H and #define SENSOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Logger.h"
#ifdef DEBUG_LOGGER
#define DL_LOG(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define DL_SerialBegin(args...) Serial.begin(args)
#else
#define DL_LOG(...)
#define DL_SerialBegin(...)
#endif

class Sensor
{
public:
  using ReadSensorCallback = std::function<float()>; // More modern typedef syntax

  // Constructor with member initializer list
  Sensor(const String &name = "",
         const String &unit = "",
         const String &type = "",
         ReadSensorCallback callback = nullptr, const uint64_t &id = -1)
      : _name(name), _unit(unit), _type(type), _callback(callback), _value(0.0f)
  {
    // Use StaticJsonDocument for small JSON objects
    setId(id);
    _sensor[F("name")] = _name;
    _sensor[F("unit")] = _unit;
    _sensor[F("type")] = _type;
  }

  [[nodiscard]] float getValue()
  {
    DL_LOG("Getting value for sensor: %s", _name.c_str());

    if (_callback)
    {
      _value = _callback();
      DL_LOG("Value: %.3f", _value);
      return _value;
    }
    return 0.0f;
  }

  [[nodiscard]] String diagnostic() const
  {
    String output;
    output.reserve(50); // Pre-allocate memory to avoid reallocations
    output += _name;
    output += F(": ");
    output += String(_value, 3); // 3 decimal precision
    output += ' ';
    output += _unit;
    return output;
  }

  void setId(const uint64_t &id)
  {
    if (id != -1)
    {
      _sensor[F("id")] = id;
    }
    _id = id;
  }
  // Getter methods marked as const and nodiscard
  [[nodiscard]] const String &getName() const { return _name; }
  [[nodiscard]] const String &getUnit() const { return _unit; }
  [[nodiscard]] const String &getType() const { return _type; }
  [[nodiscard]] const String &getId() const { return _id; }

protected:
  template <int NumSensors>
  friend class ESPLogger;

  float run(uint32_t timestamp = 0)
  { // Use uint32_t instead of u32_t for standard types
    _value = getValue();
    _lastRead = timestamp;
    return _value;
  }

  [[nodiscard]] JsonDocument getJson()
  {
    JsonDocument doc = _sensor;
    return doc;
  }

private:
  // Organize member variables by size for better memory alignment
  ReadSensorCallback _callback;
  JsonDocument _sensor; // Use StaticJsonDocument with fixed size
  String _name;
  String _unit;
  String _type;
  String _id;
  float _value;
  uint32_t _lastRead;
};