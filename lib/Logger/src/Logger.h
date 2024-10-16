#pragma once

// Configuration
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

// Debug logging macros
#ifdef DEBUG_LOGGER
#define DL_LOG(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define DL_SerialBegin(args...) Serial.begin(args)
#else
#define DL_LOG(...)
#define DL_SerialBegin(...)
#endif

// Constants
constexpr const char *SERVER_URL = "esplogger.tech";
constexpr const char *API_SUFFIX = "/api/v1";
constexpr const char *LOG_PATH = "/log";
constexpr const char *TIME_PATH = "/time";
constexpr const char *FIRMWARE_PATH = "/firmwares/download/";

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
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "Sensor.h"

template <int NumSensors>
class ESPLogger
{
public:
  // Constructor with member initializer list
  explicit ESPLogger(bool secure = true, const String &url = SERVER_URL)
      : _secure(secure), _deviceId(ESP.getEfuseMac()), _transmitting(false), _unix(0), _lastUnix(0), _sensorIntervalOffset(0), _lastSensorTimeStamp(0), _lastSensorRead(0), _lastLog(0)
  {
    // Pre-allocate string buffers
    _apiKey.reserve(40);
    _logUrl.reserve(40);
    _timeUrl.reserve(40);
    _downloadUrl.reserve(40);
    _statusUrl.reserve(60);

    _http = new HTTPClient;

    // Initialize device JSON
    _device[F("device_id")] = _deviceId;
    _setUrl(url);
    setFirmwareVersion(FIRMWARE_VERSION);
    setGroup(F("Default"));
    setDeviceName(F("ESP32"));
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }

  // Destructor to clean up resources
  ~ESPLogger()
  {
    if (_http)
    {
      delete _http;
      _http = nullptr;
    }
  }

  bool init(const String &api_key,
            const String &deviceName = F("ESP32"),
            const String &group = F("Default"),
            const String &firmwareVersion = FIRMWARE_VERSION,
            uint32_t sensorReadInterval = 30,
            uint32_t logInterval = 60)
  {
    setFirmwareVersion(firmwareVersion);
    setDeviceName(deviceName);
    setGroup(group);
    _apiKey = "Bearer " + api_key;
    setLogInterval(logInterval);
    setSensorReadInterval(sensorReadInterval);
    start();

    String payload;
    serializeJson(_device, payload);
    return _sendStatus(payload);
  }

  bool tick()
  {
    if (!_transmitting || !getUnix())
    {
      if (!getUnix())
        _syncTime();
      return false;
    }

    _updateSensors();

    if (getUnix() - _lastLog > _logInterval)
    {
      _lastLog = getUnix();
      DL_LOG("Logging data");

      for (int i = 0; i < NumSensors; i++)
      {
        if (_sensors[i])
        {
          _deviceSensors.add(_sensors[i]->getJson());
        }
      }

      String payload;
      serializeJson(_device, payload);

      if (_sendData(payload))
      {
        _lastLog = getUnix();
        return true;
      }
    }
    return false;
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
    _sensorReadInterval = constrain(interval, MIN_SENSOR_INTERVAL, MAX_SENSOR_INTERVAL);
  }

  void setLogInterval(uint32_t interval)
  {
    _logInterval = constrain(interval, MIN_INTERVAL, MAX_INTERVAL);
  }

  // Const getters
  [[nodiscard]] uint32_t getSensorReadInterval() const { return _sensorReadInterval; }
  [[nodiscard]] uint32_t getLogInterval() const { return _logInterval; }
  [[nodiscard]] const String &getFirmwareVersion() const { return _firmwareVersion; }

  void setFirmwareVersion(const String &version)
  {
    _device[F("firmware_version")] = version;
    _firmwareVersion = version;
  }

  void setDeviceName(const String &name)
  {
    _device[F("device_name")] = name;
    _deviceName = name;
  }

  void setGroup(const String &group)
  {
    _device[F("group_name")] = group;
    _deviceGroup = group;
  }

  [[nodiscard]] uint32_t getUnix()
  {
    if (_unix)
    {
      uint32_t diff = millis() - _lastUnix;
      if (diff > ONE_DAY)
      {
        _unix += diff / 1000UL;
        _lastUnix = millis() - (diff % 1000UL);
      }
      return (_unix + (millis() - _lastUnix) / 1000UL);
    }
    return 0;
  }

  // Control methods
  void setTransmitting(bool state) { _transmitting = state; }
  void stop() { _transmitting = false; }

  void start()
  {
    _lastLog = getUnix();
    _lastSensorRead = getUnix();
    _transmitting = true;
  }

  // Update callbacks
  void setOnUpdate(void (*callback)()) { httpUpdate.onStart(callback); }
  void setOnUpdateFinished(void (*callback)()) { httpUpdate.onEnd(callback); }

private:
  // Private member variables organized by size and type
  Sensor *_sensors[NumSensors] = {};
  JsonDocument _device; // Adjust size as needed
  JsonArray _deviceSensors;
  HTTPClient *_http;

  // Device identification
  uint64_t _deviceId;
  String _deviceGroup;
  String _deviceName;
  String _firmwareVersion;

  // URLs and authentication
  String _logUrl;
  String _timeUrl;
  String _downloadUrl;
  String _statusUrl;
  String _apiKey;

  // State variables
  bool _secure;
  bool _transmitting;
  uint32_t _unix;
  uint32_t _lastUnix;
  uint32_t _logInterval;
  uint32_t _sensorReadInterval;
  uint32_t _lastSensorTimeStamp;
  uint32_t _lastSensorRead;
  uint32_t _lastLog;
  uint16_t _sensorIntervalOffset;

  // Private methods
  void _resetJSON()
  {
    _deviceSensors.clear();
    _device.clear();
    _device[F("device_id")] = _deviceId;
    _device[F("device_name")] = _deviceName;
    _device[F("group_name")] = _deviceGroup;
    _device[F("firmware_version")] = _firmwareVersion;
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }

  void _updateSensors()
  {
    if (getUnix() - _lastSensorRead <= _sensorReadInterval - _sensorIntervalOffset)
    {
      return;
    }

    DL_LOG("Reading sensor values");
    uint32_t timestamp = getUnix();

    if (_lastSensorTimeStamp)
    {
      int diff = static_cast<int>(timestamp - _lastSensorTimeStamp) -
                 static_cast<int>(_sensorReadInterval);
      _sensorIntervalOffset = constrain(diff, 0, 5);
    }

    DL_LOG("Reading sensors on Timestamp: %u", timestamp);

    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        _sensors[i]->run(timestamp);
      }
    }

    _lastSensorTimeStamp = timestamp;
    _lastSensorRead = getUnix();
  }

  [[nodiscard]] bool _sendData(const String &payload)
  {
    static uint8_t retries = 0;
    DL_LOG("Sending data try: %u", retries);

    _http->begin(_logUrl);
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    _resetJSON();

    int httpCode = _http->POST(payload);
    DL_LOG("Send data HTTP Code: %d", httpCode);

    if (httpCode == 201)
    {
      retries = 0;
      if (millis() - _lastUnix > ONE_DAY)
      {
        _syncTime();
      }

      String response = _http->getString();
      DL_LOG("Response: %s", response.c_str());

      JsonDocument doc;
      deserializeJson(doc, response);
      handleNotice(doc);

      _http->end();
      return true;
    }

    _http->end();

    if (retries < 3)
    {
      retries++;
      delay(5);
      return _sendData(payload);
    }

    if (httpCode == -1 && _http)
    {
      delete _http;
      _http = new HTTPClient;
    }

    retries = 0;
    return false;
  }

  void handleNotice(const JsonDocument &doc)
  {
    if (doc[F("notice")] == F("update required"))
    {
      DL_LOG("Update required");
      String firmware_id = doc[F("firmware_id")];
      _updateFirmware(_downloadUrl + firmware_id);
    }
  }

  bool _updateFirmware(const String &downloadUrl = "")
  {
    DL_LOG("Updating firmware from: %s", downloadUrl.c_str());

    _http->begin(downloadUrl);
    _http->addHeader(F("Authorization"), _apiKey);

    t_httpUpdate_return ret = httpUpdate.update(*_http);

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
      DL_LOG("HTTP_UPDATE_FAILED Error (%d): %s",
             httpUpdate.getLastError(),
             httpUpdate.getLastErrorString().c_str());
      return false;

    case HTTP_UPDATE_NO_UPDATES:
      DL_LOG("HTTP_UPDATE_NO_UPDATES");
      return false;

    case HTTP_UPDATE_OK:
      DL_LOG("HTTP_UPDATE_OK");
      return true;

    default:
      return false;
    }
  }
  bool _sendStatus(const String &payload)
  {
    DL_LOG("Sending status");
    DL_LOG("Connecting to: %s", _statusUrl.c_str());
    _http->begin(_statusUrl);
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    int httpCode = _http->POST(payload);
    DL_LOG("Send status HTTP Code: %d", httpCode);
    if (httpCode == 200)
    {
      String response = _http->getString();
      DL_LOG("Response: %s", response.c_str());
      JsonDocument doc;
      deserializeJson(doc, response);
      _syncTime(doc[F("unix_time")]);
      handleNotice(doc);
      _http->end();
      return true;
    }
    else
    {
      _http->end();
      if (httpCode == -1 and _http)
      {
        delete _http;
        _http = new HTTPClient;
      }
      return false;
    }
  }
  bool _syncTime(const String &unix = "")
  {
    if (unix != "")
    {
      _unix = unix.toInt();
      _lastUnix = millis();
      _lastLog = _unix;
      _lastSensorRead = _unix;
      return true;
    }
    // If WiFi mode is set to AP return false
    if (WiFi.getMode() == WIFI_AP || WiFi.status() != WL_CONNECTED)
    {
      return false;
    }
    DL_LOG("Syncing time");
    //  WiFiClient client;
    DL_LOG("Connecting to: %s", _timeUrl.c_str());
    _http->begin(_timeUrl);
    DL_LOG("Adding headers");
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    // Json format -> {unix_time: 123456789}

    int httpCode = _http->GET();
    DL_LOG("Sync time HTTP Code: %d\n", httpCode);
    if (httpCode == 200)
    {
      String payload = _http->getString();

      DL_LOG("Payload: %s", payload.c_str());
      JsonDocument doc;
      DL_LOG("Deserializing");
      deserializeJson(doc, payload);
      DL_LOG("Deserialized");
      _unix = doc[F("unix_time")];
      _lastUnix = millis();
      _lastLog = _unix;
      _lastSensorRead = _unix;
      _http->end();
      return true;
    }
    else
    {
      _http->end();
      if (httpCode == -1 and _http)
      {
        delete _http;
        _http = new HTTPClient;
      }
      return false;
    }
  }
  void _setUrl(const String &url)
  {
    String _prefix = F("https://");
    if (!_secure)
      _prefix = F("http://");
    _logUrl = _prefix + url + API_SUFFIX + LOG_PATH;
    _timeUrl = _prefix + url + API_SUFFIX + TIME_PATH;
    _downloadUrl = _prefix + url + API_SUFFIX + FIRMWARE_PATH;
    _statusUrl = _prefix + url + API_SUFFIX + F("/devices/") + String(_deviceId) + F("/status");
  }
};