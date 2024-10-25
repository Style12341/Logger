#pragma once

// Configuration
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

// Debug logging macros
#define DEBUG_LOGGER
#ifdef DEBUG_LOGGER
#define DL_LOG(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define DL_SerialBegin(args...) Serial.begin(args)
#else
#define DL_LOG(...)
#define DL_SerialBegin(...)
#endif

// Constants
constexpr const char *SERVER_URL_L = "esplogger.tech";
constexpr const char *API_SUFFIX_L = "/socket/api/v1";

// Time constants
constexpr uint32_t MAX_INTERVAL = 3600;
constexpr uint32_t MIN_INTERVAL = 60;
constexpr uint32_t MAX_SENSOR_INTERVAL = 1800;
constexpr uint32_t MIN_SENSOR_INTERVAL = 10;
constexpr uint32_t ONE_DAY = 86400000UL;

// Forward declarations
class Sensor;
template <int NumSensors>
class ESPLogger;

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPPhoenixSocket.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "Sensor.h"
#include "LoggerClient.h"

template <int NumSensors>
class ESPLogger
{
public:
  // Constructor with member initializer list
  explicit ESPLogger(bool secure = true, const String &url = SERVER_URL_L)
      : _secure(secure), _deviceId(ESP.getEfuseMac()), _transmitting(false), _serverUrl(url), _lastUnix(0), _sensorIntervalOffset(0), _lastSensorTimeStamp(0), _lastSensorRead(0)
  {
    // Pre-allocate string buffers
    _apiKey.reserve(40);
    // Initialize device JSON
    setFirmwareVersion(FIRMWARE_VERSION);
    setGroup(F("Default"));
    setDeviceName(F("ESP32"));
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }

  // Destructor to clean up resources
  ~ESPLogger()
  {
    if (_socket)
    {
      delete _socket;
      _socket = nullptr;
    }
  }

  void init(const String &api_key,
            const String &deviceName = F("ESP32"),
            const String &group = F("Default"),
            const String &firmwareVersion = FIRMWARE_VERSION,
            uint32_t sensorReadInterval = 30)
  {
    setFirmwareVersion(firmwareVersion);
    setDeviceName(deviceName);
    setGroup(group);
    setApiKey(api_key);
    setSensorReadInterval(sensorReadInterval);
    _addSensorMetadata();
    _client = new LoggerClient(_apiKey, _serverUrl);
    _client->setAfterJoinCallback([this](const int64_t group_id, JsonDocument sensor_ids)
                                  { _setIds(group_id, sensor_ids); });
    start();
    String payload;
    serializeJson(_device, payload);
    _client->joinChannel(payload);
  }

  bool tick()
  {
    if (!_transmitting || !getUnix())
    {
      return false;
    }

    _tickSensors();
    return true;
  }

  [[nodiscard]] String sensorsDiagnostic() const
  {
    String output;
    output.reserve(NumSensors * 50); // Estimate 50 chars per sensor

    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        output += _sensors[i]->diagnostic() + '\n';
      }
    }
    return output;
  }

  bool addSensor(const String &name, const String &unit,
                 const String &type, std::function<float()> callback)
  {
    return addSensor(Sensor(name, unit, type, callback));
  }

  bool addSensor(Sensor &sensor)
  {
    for (int i = 0; i < NumSensors; i++)
    {
      if (!_sensors[i])
      {
        _sensors[i] = &sensor;
        return true;
      }
    }
    return false;
  }

  // Setters with validation
  void setSensorReadInterval(uint32_t interval)
  {
    _sensorReadInterval = constrain(interval, MIN_SENSOR_INTERVAL, MAX_SENSOR_INTERVAL) * 1000;
  }

  // Const getters
  [[nodiscard]] uint32_t getSensorReadInterval() const { return _sensorReadInterval; }
  [[nodiscard]] const String &getFirmwareVersion() const { return _firmwareVersion; }

  void setApiKey(const String &key)
  {
    _device[F("api_token")] = key;
  }
  void setFirmwareVersion(const String &version)
  {
    _device[F("device")][F("firmware_version")] = version;
    _firmwareVersion = version;
  }

  void setDeviceName(const String &name)
  {
    _device[F("device")][F("name")] = name;
    _deviceName = name;
  }

  void setGroup(const String &group, const int groupId = -1)
  {
    _device[F("group")][F("name")] = group;
    if (groupId != -1)
      _device[F("group")][F("id")] = groupId;
    _deviceGroup = group;
  }

  [[nodiscard]] uint32_t getUnix()
  {
    return _client->getUnix();
  }

  // Control methods
  void setTransmitting(bool state) { _transmitting = state; }
  void stop() { _transmitting = false; }
  void start()
  {
    _lastSensorRead = millis();
    _transmitting = true;
  }

private:
  // Private member variables organized by size and type
  Sensor *_sensors[NumSensors] = {};
  JsonDocument _device; // Adjust size as needed
  JsonArray _deviceSensors;
  PhoenixSocket *_socket;
  LoggerClient *_client;

  // Device identification
  uint64_t _deviceId;
  String _deviceGroup;
  uint64_t _groupId;
  String _deviceName;
  String _firmwareVersion;

  // URLs and authentication
  String _serverUrl;
  String _apiKey;

  // State variables
  bool _secure;
  bool _transmitting;
  uint32_t _lastUnix;
  uint32_t _sensorReadInterval;
  uint32_t _lastSensorTimeStamp;
  uint32_t _lastSensorRead;
  uint16_t _sensorIntervalOffset;

  // Private methods
  void _setIds(const int64_t group_id, JsonDocument sensor_ids)
  {
    _groupId = group_id;
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        _sensors[i]->setId((uint64_t)sensor_ids[i]);
      }
    }
  }
  void _addSensorMetadata()
  {
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        _deviceSensors.add(_sensors[i]->getJson());
      }
    }
  }

  void _resetJSON()
  {
    _deviceSensors.clear();
    _device.clear();
    setApiKey(_apiKey);
    setDeviceName(_deviceName);
    setFirmwareVersion(_firmwareVersion);
    setGroup(_deviceGroup);
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
    _addSensorMetadata();
  }

  void _tickSensors()
  {
    if (millis() - _lastSensorRead <= _sensorReadInterval)
    {
      return;
    }
    static float sensorValues[NumSensors] = {};

    DL_LOG("Reading sensor values");
    uint32_t timestamp = getUnix();
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        sensorValues[i] = _sensors[i]->run();
      }
    }

    _lastSensorTimeStamp = timestamp;
    _lastSensorRead = millis();
  }
  void _dispatchSensorValues(float values[NumSensors])
  {
    DL_LOG("Dispatching sensor values");
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        _client->sendSensorData(values[i], _sensors[i]->getId());
      }
    }
  }
};