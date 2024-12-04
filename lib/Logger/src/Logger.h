#pragma once
#ifndef LOGGER_H
#define LOGGER_H
// Configuration
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

// Debug logging macros
// #define DEBUG_LOGGER
#ifdef DEBUG_LOGGER
#define DL_LOG(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define DL_SerialBegin(args...) Serial.begin(args)
#else
#define DL_LOG(...)
#define DL_SerialBegin(...)
#endif

// Constants
constexpr const char *SERVER_URL_L = "esplogger.tech";
constexpr const char *API_SUFFIX_L = "/socket/api/v1/websocket";
constexpr const uint16_t PORT = 4000;

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
  explicit ESPLogger(const String &url = SERVER_URL_L, const uint16_t &port = PORT)
      : _transmitting(false), _serverUrl(url), _serverPort(port), _lastUnix(0), _sensorIntervalOffset(0), _lastSensorTimeStamp(0), _lastSensorRead(0)
  {
    // Pre-allocate string buffers
    _apiKey.reserve(50);
    // Initialize device JSON
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }

  // Destructor to clean up resources
  ~ESPLogger()
  {
    if (_client)
    {
      delete _client;
      _client = nullptr;
    }
  }

  void begin(const String &api_key,
             const String &deviceName = F("ESP32"),
             const String &group = F("Default"),
             const String &firmwareVersion = FIRMWARE_VERSION,
             uint32_t sensorPollInterval = 30)
  {
    // Set up private variables and JSON
    setDeviceId(ESP.getEfuseMac());
    setFirmwareVersion(firmwareVersion);
    setDeviceName(deviceName);
    setGroup(group);
    setApiKey(api_key);
    setSensorPollInterval(sensorPollInterval);
    _addSensorMetadata();
    DL_LOG("[Logger] Starting logger with API key %s", _apiKey.c_str());
    _client = new LoggerClient(_deviceId, _apiKey, _serverUrl, _serverPort);
    _client->setAfterJoinCallback([this](const int64_t group_id, JsonDocument sensor_ids)
                                  { _setIds(group_id, sensor_ids); });
    start();
    String payload;
    serializeJson(_device, payload);
    DL_LOG("[Logger]Join payload\n\t\t%s\n\n", payload.c_str());
    _client->setJoinString(payload);
    _device.clear();
  }

  bool tick()
  {
    if (!_transmitting)
    {
      return false;
    }
    _client->tick();
    if (!_client->isChannelJoined())
    {
      DL_LOG("[Logger]Channel has not been joined yet");
      return false;
    }
    if (!getUnix())
    {
      DL_LOG("[Logger]Time has not been received from server");
    }
    _tickSensors();
    return true;
  }

  [[nodiscard]] String sensorsDiagnostic() const
  {
    String output;
    output.reserve(NumSensors * 50); // Estimate 50 chars per sensor
    DL_LOG("[Logger]Generating sensors diagnostic");
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
    DL_LOG("[Logger]Adding sensor %s of type %s", sensor._name, sensor._type);
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
  void setSensorPollInterval(uint32_t interval)
  {
    _sensorReadInterval = constrain(interval, MIN_SENSOR_INTERVAL, MAX_SENSOR_INTERVAL) * 1000;
    DL_LOG("[Logger]Setting sensor read interval to %d", _sensorReadInterval);
  }

  // Const getters
  [[nodiscard]] uint32_t getSensorReadInterval() const { return _sensorReadInterval; }
  [[nodiscard]] const String &getFirmwareVersion() const { return _firmwareVersion; }

  void setApiKey(const String &key)
  {
    _device[F("api_token")] = key;
    _apiKey = key;
  }
  void setFirmwareVersion(const String &version)
  {
    DL_LOG("[Logger]Setting firmware version %s", version);
    _device[F("device")][F("firmware_version")] = version;
    _firmwareVersion = version;
  }

  void setDeviceName(const String &name)
  {
    DL_LOG("[Logger]Setting device name %s", name);
    _device[F("device")][F("name")] = name;
    _deviceName = name;
  }
  void setDeviceId(const uint64_t &id)
  {
    DL_LOG("[Logger]Setting device id %d", id);
    _device[F("device")][F("id")] = id;
    _deviceId = id;
  }
  void setGroup(const String &group, const int groupId = -1)
  {
    DL_LOG("[Logger]Setting group name %s", group);
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
  void stop()
  {
    DL_LOG("[Logger]setting transmission to false");
    _transmitting = false;
  }
  void start()
  {
    _lastSensorRead = millis();
    DL_LOG("[Logger]setting transmission to true");
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
  uint16_t _serverPort;

  // State variables
  bool _secure;
  bool _transmitting;
  bool _hasSentValues = false;
  uint32_t _lastUnix;
  uint32_t _sensorReadInterval;
  uint32_t _lastSensorTimeStamp;
  uint32_t _lastSensorRead;
  uint16_t _sensorIntervalOffset;

  // Private methods
  void _setIds(const int64_t group_id, JsonDocument sensor_ids)
  {
    _groupId = group_id;
    JsonArray sensor_ids_array = sensor_ids.as<JsonArray>();
    int i = 0;
    for (JsonVariant sensor_id : sensor_ids_array)
    {
      DL_LOG("[Logger]Sensor id received: %d", sensor_id.as<uint64_t>());
      if (_sensors[i])
      {
        _sensors[i]->setId(sensor_id.as<uint64_t>());
      }
      i++;
    }
  }
  void _addSensorMetadata()
  {
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        DL_LOG("[Logger]Setting sensor %d metadata to join payload", i);
        _deviceSensors.add(_sensors[i]->getJson());
      }
    }
  }

  void _resetJSON()
  {
    DL_LOG("[Logger]Ressetting JSON");
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
    if (_hasSentValues && millis() - _lastSensorRead <= _sensorReadInterval)
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
    uint32_t currTime = millis();
    _dispatchSensorValues(sensorValues);
    _hasSentValues = true;
    uint32_t timeDiff = millis() - currTime;
    Serial.printf("Sensors read and dispatch took %d ms", timeDiff);
    Serial.printf("Average delay between sensor dispatch: %d ms", timeDiff / NumSensors);
  }
  void _dispatchSensorValues(const float values[NumSensors])
  {
    DL_LOG("Dispatching sensor values");
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        DL_LOG("Dispatching sensor %d value %.2f", i, values[i]);
        _client->sendSensorData(values[i], _sensors[i]->getId());
      }
    }
  }
};
#endif